// Development tool: loads an embedded amp and prints its DC operating point and a few
// measurements, so a freshly transcribed schematic can be sanity-checked against the drawing.
//   sas_inspect <amp id>
#include <cmath>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "sas/amp.h"
#include "sas/embedded_amps.h"
#include "sas/netlist.h"
#include "sas/spring.h"
#include "sas/vibrato.h"

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

// sas_inspect --dc <netlist file> <rail volts> [rail source name]: node voltages of the DC point of any netlist.
static int dumpDc(int argc, char** argv) {
    std::ifstream f(argv[2]);
    std::stringstream ss;
    ss << f.rdbuf();
    auto net = sas::parseNetlist(ss.str(), argv[2]);
    net.circuit.setSourceDc(net.source(argc > 4 ? argv[4] : "VRAIL"), std::atof(argv[3]));
    sas::Solver solver(std::move(net.circuit), 96000.0);
    for (const auto& [name, node] : net.nodes) std::printf("%-8s %12.5f\n", name.c_str(), solver.voltage(node));
    return solver.dcConverged ? 0 : 2;
}

static int run(int argc, char** argv) {
    if (argc > 3 && std::string(argv[1]) == "--dc") return dumpDc(argc, argv);
    const std::string id = argc > 1 ? argv[1] : "plexi_1959";
    const sas::AmpDef def = sas::loadEmbeddedAmp(id);
    std::printf("amp %s (%s)\n\n", def.id.c_str(), def.name.c_str());

    // Power amp DC point.
    sas::PowerAmp power(def, kFs);
    const auto net = sas::parseNetlist(def.power.netlistText, def.power.netlist);
    auto v = [&](const char* a, const char* b = nullptr, const char* c = nullptr) {
        for (const char* n : {a, b, c}) if (n && net.nodes.count(n)) return power.solver().voltage(net.node(n));
        return std::nan("");
    };
    std::printf("power amp, idle after settling:\n");
    std::printf("  B+ (a)           %8.1f V\n", v("a"));
    std::printf("  rail x           %8.1f V\n", v("x"));
    const double ipair = (v("a") - v("pt")) / (def.id == "twin_65" ? 35.0 : 40.0);
    std::printf("  plate pt         %8.1f V   idle %.1f mA per tube (pair via DCR)\n", v("pt"), ipair / 2 * 1e3);
    std::printf("  output tube grid %8.1f V\n", v("g45", "g12", "g1"));
    std::printf("  screen           %8.1f V\n", v("s45", "s12", "s1"));
    std::printf("  PI plates        %8.1f / %.1f V\n", v("pa"), v("pb"));
    std::printf("  PI cathode kc    %8.1f V\n", v("kc", "k"));
    std::printf("  bias node        %8.1f V\n", v("bias", "cbias"));
    std::printf("  failures         %llu   (dc converged: %s)\n\n", (unsigned long long)power.solver().newtonFailures, power.solver().dcConverged ? "yes" : "NO");
    if (std::getenv("SAS_DUMP_POWER"))
        for (const auto& [name, node] : net.nodes) std::printf("    pow %-6s %9.2f V\n", name.c_str(), power.solver().voltage(node));
    if (std::getenv("SAS_TRACE_POWER")) {
        sas::PowerAmp p3(def, kFs);
        std::printf("  trace (ms: wp pt nb na t8):\n");
        for (std::size_t i = 0; i < std::size_t(0.05 * kFs); ++i) {
            p3.process(0.0);
            if (i % 96 == 0 && net.nodes.count("wp"))
                std::printf("   %5.1f  %9.3f %9.3f %9.3f %9.3f %9.3f\n", 1e3 * double(i) / kFs, p3.solver().voltage(net.node("wp")), p3.solver().voltage(net.node("pt")),
                            p3.solver().voltage(net.node("nb")), p3.solver().voltage(net.node("na")), p3.solver().voltage(net.node("t8")));
        }
    }
    if (std::getenv("SAS_DUMP_POWER")) {
        // Is it really at rest? Run silence and report the peak-to-peak of a few nodes.
        const char* watch[] = {"t8", "pga", "wp", "na", "pt"};
        for (const char* w : watch) {
            if (!net.nodes.count(w)) continue;
            double lo = 1e18, hi = -1e18;
            sas::PowerAmp p2(def, kFs);
            for (std::size_t i = 0; i < std::size_t(0.3 * kFs); ++i) p2.process(0.0);
            for (std::size_t i = 0; i < std::size_t(0.1 * kFs); ++i) { p2.process(0.0); const double x = p2.solver().voltage(net.node(w)); lo = std::min(lo, x); hi = std::max(hi, x); }
            std::printf("    silence %-4s min %9.3f max %9.3f\n", w, lo, hi);
        }
    }

    // Preamp DC point at the power amp's rail.
    sas::Preamp pre(def, kFs, power.railX());
    const auto pnet = sas::parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    std::printf("preamp node voltages:\n");
    for (const auto& [name, node] : pnet.nodes) std::printf("  %-6s %8.2f V\n", name.c_str(), pre.solver().voltage(node));

    if (def.preamp.vibrato.present) {
        std::printf("\nvibrato (photocell ohms, footswitch on, 12 s):\n");
        for (double speed : {0.1, 0.3, 0.5, 0.8, 1.0}) {
            sas::Vibrato v(def.preamp.vibrato, kFs, power.railX(), false);
            v.setSpeed(speed, 100.0);
            v.setOn(true);
            const double stepHz = kFs / double(v.controlSamples());
            std::vector<double> r;
            for (std::size_t i = 0; i < std::size_t(12.0 * stepHz); ++i) r.push_back(v.step());
            const std::size_t from = r.size() / 2;
            double lo = 1e18, hi = 0, mean = 0;
            for (std::size_t i = from; i < r.size(); ++i) { lo = std::min(lo, r[i]); hi = std::max(hi, r[i]); mean += std::log(r[i]); }
            mean = std::exp(mean / double(r.size() - from));
            int cross = 0;
            for (std::size_t i = from + 1; i < r.size(); ++i) if (r[i - 1] < mean && r[i] >= mean) ++cross;
            std::printf("  speed %.1f: %7.0f .. %7.0f ohms, %.2f Hz, failures %llu\n", speed, lo, hi, cross / (double(r.size() - from) / stepHz), (unsigned long long)v.newtonFailures());
            if (speed == 0.5) {
                std::printf("    start-up (0.25 s steps):");
                for (std::size_t i = 0; i < r.size() && i < std::size_t(6.0 * stepHz); i += std::size_t(0.25 * stepHz)) std::printf(" %.0f", r[i]);
                std::printf("\n");
            }
        }
        // Depth of the effect in the audio: 20 ms windows of the output rms for a steady 220 Hz tone.
        const std::size_t ik = def.knobIndex("intensity");
        for (double inten : {0.0, 0.5, 1.0}) {
            for (double speed : {0.2, 0.5}) {
                sas::FullAmp a(def, kFs);
                std::vector<double> k;
                for (const auto& kd : def.preamp.knobs) k.push_back(kd.def);
                k[ik] = inten;
                a.setKnobs(k, 0.5);
                a.setJack(def.jackIndex("vibrato1"));
                a.pre().setVibrato(true, speed);
                const std::size_t win = std::size_t(0.02 * kFs), total = std::size_t(8.0 * kFs);
                std::vector<double> rms;
                double acc = 0;
                for (std::size_t i = 0; i < total; ++i) {
                    const double y = a.process(0.02 * std::sin(2 * kPi * 220.0 * double(i) / kFs));
                    acc += y * y;
                    if ((i + 1) % win == 0) { rms.push_back(std::sqrt(acc / double(win))); acc = 0; }
                }
                double lo = 1e9, hi = 0;
                for (std::size_t i = rms.size() / 2; i < rms.size(); ++i) { lo = std::min(lo, rms[i]); hi = std::max(hi, rms[i]); }
                std::printf("  intensity %.1f speed %.1f: output %.2f .. %.2f V rms (depth %.1f dB)\n", inten, speed, lo, hi, 20.0 * std::log10(hi / std::max(lo, 1e-9)));
            }
        }
    }

    if (def.preamp.reverb.present) {
        // The schematic's test point levels: 1 kHz, 37 mV rms at the Vibrato input, controls full clockwise.
        std::printf("\nreverb test points (1 kHz, 37 mV rms in, vibrato channel full, reverb full):\n");
        sas::Preamp p(def, kFs, power.railX());
        std::vector<double> full(def.preamp.knobs.size(), 1.0);
        full[def.knobIndex("vibrato_volume")] = 0.35;  // the schematic does not say; chosen so that V2B plate is near TP6 (3.1 V)
        p.setKnobs(full);
        p.setJack(def.jackIndex("vibrato1"));
        std::map<std::string, double> acc, mean;
        const char* names[] = {"qa", "gv3", "pv4m", "rt", "q"};
        const std::size_t n = std::size_t(1.5 * kFs);
        std::size_t cnt = 0;
        std::map<std::string, std::vector<double>> tr;
        for (std::size_t i = 0; i < n; ++i) {
            p.process(0.0523 * std::sin(2 * kPi * 1000.0 * double(i) / kFs));
            if (i < n / 2) continue;
            ++cnt;
            for (const char* nm : names) tr[nm].push_back(p.solver().voltage(pnet.node(nm)));
        }
        for (const char* nm : names) {
            double m = 0; for (double x : tr[nm]) m += x; m /= double(cnt);
            double s = 0; for (double x : tr[nm]) s += (x - m) * (x - m);
            std::printf("  %-4s %8.4f V rms\n", nm, std::sqrt(s / double(cnt)));
        }
        {
            // Average magnitude of the tank around 1 kHz (sine sweep), relative to the configured gain.
            double sum = 0; int cnt = 0;
            for (double f = 800.0; f <= 1250.0; f += 12.5) {
                sas::SpringTank t(def.preamp.reverb, kFs);
                double acc2 = 0; std::size_t m = 0;
                const std::size_t total = std::size_t(4.0 * kFs);
                for (std::size_t i = 0; i < total; ++i) {
                    const double y = t.process(std::sin(2 * kPi * f * double(i) / kFs));
                    if (i > total / 2) { acc2 += y * y; ++m; }
                }
                sum += std::sqrt(acc2 / double(m)) * std::sqrt(2.0);
                ++cnt;
            }
            std::printf("  tank gain around 1 kHz (mean magnitude): %.5f (target 0.00173)\n", sum / cnt);
        }
        {
            // Decay of the tank alone: energy of 50 ms windows after an impulse, time to fall 30 dB, doubled.
            sas::SpringTank t(def.preamp.reverb, kFs);
            const std::size_t win = std::size_t(0.05 * kFs), nwin = 120;
            std::vector<double> e(nwin, 0.0);
            for (std::size_t i = 0; i < win * nwin; ++i) {
                const double y = t.process(i == 0 ? 1.0 : 0.0);
                e[i / win] += y * y;
            }
            double peak = 0; for (double v : e) peak = std::max(peak, v);
            double rt = -1;
            for (std::size_t w = 0; w < nwin; ++w) if (e[w] < peak * 1e-3 && w > 4) { rt = 2.0 * double(w) * 0.05; break; }
            std::printf("  tank decay: RT60 ~ %.2f s (-30 dB at window %.2f s)\n", rt, rt / 2);
            std::printf("  energy per 50 ms (dB re peak):");
            for (std::size_t w = 0; w < 24; ++w) std::printf(" %.0f", 10.0 * std::log10(std::max(e[w], 1e-30) / peak));
            std::printf("\n");
        }
    }

    if (std::getenv("SAS_SWEEP")) {
        std::printf("\npower amp alone, 440 Hz at the phase inverter input (peak volts -> output rms at the 8 ohm tap, grid mean):\n");
        for (double a : {0.05, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 40.0}) {
            sas::PowerAmp p4(def, kFs);
            double acc = 0, gm = 0; std::size_t cnt = 0;
            const std::size_t total = std::size_t(0.6 * kFs);
            for (std::size_t i = 0; i < total; ++i) {
                const double y = p4.process(a * std::sin(2 * kPi * 440.0 * double(i) / kFs));
                if (i > total / 2) { acc += y * y; gm += p4.solver().voltage(net.node("pga")); ++cnt; }
            }
            std::printf("  %6.2f V -> %7.2f V rms (%.0f W into 8 ohm), pga mean %.1f V\n", a, std::sqrt(acc / double(cnt)), acc / double(cnt) / 8.0, gm / double(cnt));
        }
    }
    if (std::getenv("SAS_FAILS")) {
        for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) {
            sas::FullAmp a(def, kFs);
            std::vector<double> k;
            for (const auto& kd : def.preamp.knobs) k.push_back(kd.def);
            a.setKnobs(k, def.power.presenceDefault);
            a.setJack(j);
            std::size_t firstFail = 0; for (std::size_t i = 0; i < std::size_t(0.5 * kFs); ++i) { a.process(0.1 * std::sin(2 * kPi * 220.0 * double(i) / kFs)); if (!firstFail && a.power().solver().newtonFailures) firstFail = i; }
            std::printf("  first failure at sample %zu (%.2f ms)\n", firstFail, 1e3 * double(firstFail) / kFs);
            std::printf("  fails %-8s power %llu", def.preamp.jacks[j].name.c_str(), (unsigned long long)a.power().solver().newtonFailures);
            for (std::size_t s = 0; s < a.pre().stageCount(); ++s) std::printf("  stage%zu %llu (cont %llu)", s + 1, (unsigned long long)a.pre().solver(s).newtonFailures, (unsigned long long)a.pre().solver(s).continuationRuns);
            std::printf("\n");
        }
    }
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
    const char* preNodes[] = {"m1", "p4", "pb1", "pb2", "gb1", "gb2", "p5", "v1w", "p6a", "p6b", "p7a", "k7b", "so", "k8", "p8", "mo", "y", "o"};
    const char* powNodes[] = {"pin", "gp", "ga", "pga", "g1", "t4", "pa", "pb", "g12", "g45", "pt", "t16"};
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
