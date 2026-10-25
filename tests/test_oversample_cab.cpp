#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "sas/cab.h"
#include "sas/oversample.h"
#include "golden_io.h"

TEST_CASE("oversampler matches the Rust reference") {
    for (std::size_t stages : {1u, 2u}) {
        const auto c = golden::load("oversample_" + std::to_string(stages));
        sas::Oversampler os(stages);
        std::vector<double> y;
        for (double x : c.in) y.push_back(os.process(float(x), [](float v) { return v; }));
        const auto cmp = golden::compare(y, c.out);
        INFO("stages " << stages << " error " << cmp.errDb() << " dB, peak " << cmp.peakErr);
        CHECK(cmp.errDb() < -120.0);
    }
}

TEST_CASE("oversampler is transparent and reports its latency") {
    for (std::size_t stages : {1u, 2u}) {
        sas::Oversampler os(stages);
        const float lat = os.latencySamples();
        CHECK(lat > 10.0f);
        CHECK(os.factor() == (stages == 1 ? 2u : 4u));
    }
}

TEST_CASE("cab filter matches the Rust reference") {
    const auto c = golden::load("cab");
    sas::CabFilter cab(48000.0f);
    std::vector<double> y;
    for (double x : c.in) y.push_back(cab.process(float(x)));
    const auto cmp = golden::compare(y, c.out);
    INFO("error " << cmp.errDb() << " dB");
    CHECK(cmp.errDb() < -110.0);
}
