// The AFD100 preamp is solved in two parts (preamp.net up to the tone stack, master.net after it).
// The monolithic circuit is kept in tests/data as the reference: the split must stay close to it in
// DC and across frequency and level (set SAS_SPLIT_VERBOSE=1 to print the error table).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <sstream>

#include "sas/embedded_amps.h"
#include "sas/netlist.h"
#include "sas/preamp.h"

using namespace sas;

#ifndef SAS_TEST_DATA_DIR
#error "SAS_TEST_DATA_DIR must point at tests/data"
#endif

namespace {

constexpr double kFs = 96000.0, kPi = 3.14159265358979323846, kRail = 467.4;

AmpDef monolithic() {
    AmpDef def = loadEmbeddedAmp("afd100");
    std::ifstream f(std::string(SAS_TEST_DATA_DIR) + "/afd100_preamp_full.net");
    std::stringstream ss;
    ss << f.rdbuf();
    def.preamp.netlistText = ss.str();
    def.preamp.stages.clear();
    return def;
}

double outputRms(const AmpDef& def, std::size_t jack, const std::vector<double>& knobs, double hz, double peak, bool eco = false) {
    Preamp pre(def, kFs, kRail, eco);
    pre.setKnobs(knobs);
    pre.setJack(jack);
    const std::size_t n = std::size_t(0.6 * kFs), from = n / 2;
    double acc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double y = pre.process(peak * std::sin(2 * kPi * hz * double(i) / kFs));
        if (i >= from) acc += y * y;
    }
    REQUIRE(pre.newtonFailures() == 0);
    return std::sqrt(acc / double(n - from));
}

}  // namespace

TEST_CASE("AFD100 split preamp has two stages joined at the tone stack output") {
    const AmpDef def = loadEmbeddedAmp("afd100");
    REQUIRE(def.preamp.stages.size() == 1);
    Preamp pre(def, kFs, kRail);
    CHECK(pre.stageCount() == 2);
    // Ten non-linear ports before the cut (V5..V7B) and four after it (V8A, V8B).
    CHECK(pre.solver(0).portCount() == 10);
    CHECK(pre.solver(1).portCount() == 4);
    CHECK(pre.solver(1).nodeCount() < 20);
}

TEST_CASE("AFD100 split preamp matches the monolithic circuit") {
    const AmpDef split = loadEmbeddedAmp("afd100"), full = monolithic();

    {  // DC: the second part starts from the first part's V7B cathode and the same rails.
        Preamp a(split, kFs, kRail), b(full, kFs, kRail);
        const auto fnet = parseNetlist(full.preamp.netlistText, "full");
        const auto n1 = parseNetlist(split.preamp.netlistText, "a");
        const auto n2 = parseNetlist(split.preamp.stages[0].netlistText, "b");
        for (const char* node : {"h4", "h5", "h6", "p6a", "p7a", "k7b"})
            CHECK(std::abs(a.solver(0).voltage(n1.node(node)) - b.solver(0).voltage(fnet.node(node))) < 0.3);
        for (const char* node : {"k8", "p8", "h4"})
            CHECK(std::abs(a.solver(1).voltage(n2.node(node)) - b.solver(0).voltage(fnet.node(node))) < 0.3);
    }

    std::vector<double> knobs;
    for (const auto& k : split.preamp.knobs) knobs.push_back(k.def);
    double worstSmall = 0, worstLarge = 0;
    for (std::size_t jack = 0; jack < 2; ++jack)
        for (double hz : {100.0, 400.0, 1000.0, 3000.0, 8000.0})
            for (double peak : {0.005, 0.1}) {
                const double a = outputRms(split, jack, knobs, hz, peak), b = outputRms(full, jack, knobs, hz, peak);
                const double errDb = 20.0 * std::log10(a / b);
                INFO("jack " << jack << ", " << hz << " Hz, " << peak << " V: split " << a << ", full " << b << " (" << errDb << " dB)");
                if (std::getenv("SAS_SPLIT_VERBOSE"))
                    std::printf("jack %zu %6.0f Hz %5.3f V: split %9.5f full %9.5f  %+6.2f dB\n", jack, hz, peak, a, b, errDb);
                (peak < 0.05 ? worstSmall : worstLarge) = std::max(peak < 0.05 ? worstSmall : worstLarge, std::abs(errDb));
            }
    INFO("worst error: small signal " << worstSmall << " dB, large " << worstLarge << " dB");
    CHECK(worstSmall < 0.5);
    CHECK(worstLarge < 0.7);

    // Other tone settings load the follower differently (treble and bass extremes).
    for (const auto& [bass, middle, treble] : {std::tuple{0.0, 0.5, 1.0}, std::tuple{1.0, 0.2, 0.0}}) {
        auto k = knobs;
        k[split.knobIndex("bass")] = bass;
        k[split.knobIndex("middle")] = middle;
        k[split.knobIndex("treble")] = treble;
        for (double hz : {100.0, 1000.0, 5000.0}) {
            const double a = outputRms(split, 0, k, hz, 0.02), b = outputRms(full, 0, k, hz, 0.02);
            INFO("tone " << bass << "/" << middle << "/" << treble << " at " << hz << " Hz: " << 20.0 * std::log10(a / b) << " dB");
            CHECK(std::abs(20.0 * std::log10(a / b)) < 0.7);
        }
    }
}

TEST_CASE("AFD100 Eco preamp (three parts, tables) matches the monolithic circuit") {
    const AmpDef eco = loadEmbeddedAmp("afd100"), full = monolithic();
    REQUIRE(eco.hasEcoPreamp);
    {
        Preamp pre(eco, kFs, kRail, true);
        REQUIRE(pre.stageCount() == 3);
        CHECK(pre.solver(0).portCount() == 2);   // V5
        CHECK(pre.solver(1).portCount() == 8);   // V6A, V6B, V7A, V7B
        CHECK(pre.solver(2).portCount() == 4);   // V8A, V8B
        // DC: V5's plate and V7B's cathode agree with the whole circuit.
        Preamp ref(full, kFs, kRail);
        const auto fnet = parseNetlist(full.preamp.netlistText, "full");
        const auto n1 = parseNetlist(eco.ecoPreamp.netlistText, "eco1");
        const auto n2 = parseNetlist(eco.ecoPreamp.stages[0].netlistText, "eco2");
        CHECK(std::abs(pre.solver(0).voltage(n1.node("p5")) - ref.solver(0).voltage(fnet.node("p5"))) < 0.3);
        for (const char* node : {"h4", "h5", "h6", "p6a", "p7a", "k7b"})
            CHECK(std::abs(pre.solver(1).voltage(n2.node(node)) - ref.solver(0).voltage(fnet.node(node))) < 0.5);
    }
    std::vector<double> knobs;
    for (const auto& k : eco.preamp.knobs) knobs.push_back(k.def);
    double worstSmall = 0, worstLarge = 0;
    for (std::size_t jack = 0; jack < 2; ++jack)
        for (double hz : {100.0, 400.0, 1000.0, 3000.0, 8000.0})
            for (double peak : {0.005, 0.1}) {
                const double a = outputRms(eco, jack, knobs, hz, peak, true), b = outputRms(full, jack, knobs, hz, peak);
                const double errDb = 20.0 * std::log10(a / b);
                if (std::getenv("SAS_SPLIT_VERBOSE"))
                    std::printf("eco jack %zu %6.0f Hz %5.3f V: eco %9.5f full %9.5f  %+6.2f dB\n", jack, hz, peak, a, b, errDb);
                (peak < 0.05 ? worstSmall : worstLarge) = std::max(peak < 0.05 ? worstSmall : worstLarge, std::abs(errDb));
            }
    INFO("worst error: small signal " << worstSmall << " dB, large " << worstLarge << " dB");
    CHECK(worstSmall < 0.7);
    CHECK(worstLarge < 1.0);
}
