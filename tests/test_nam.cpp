#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "sas/nam_model.h"

using namespace sas;

namespace {
const std::string kModels = SAS_NAM_EXAMPLES_DIR;

std::vector<float> sine(double hz, double amp, std::size_t n, double fs) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = float(amp * std::sin(2.0 * 3.14159265358979 * hz * double(i) / fs));
    return v;
}
}  // namespace

TEST_CASE("nam: a WaveNet model loads, reports its metadata and plays a finite, non-silent signal") {
    std::string err;
    auto m = NamModel::load(kModels + "/wavenet.nam", 48000.0, 512, err);
    INFO(err);
    REQUIRE(m != nullptr);
    CHECK(m->name() == "wavenet");
    CHECK(m->hasLoudness());
    CHECK(m->normalizeGain() > 0.0f);
    CHECK(m->trainedRate() == 48000.0);
    CHECK(m->rateMatches());

    const auto in = sine(220.0, 0.2, 9600, 48000.0);
    std::vector<float> out(in.size());
    // Blocks of an odd size, bigger and smaller than the prepared size.
    for (std::size_t at = 0; at < in.size();) {
        const std::size_t n = std::min<std::size_t>(in.size() - at, at % 3 ? 700 : 37);
        m->process(in.data() + at, out.data() + at, n);
        at += n;
    }
    double e = 0.0;
    for (std::size_t i = 4800; i < out.size(); ++i) {
        REQUIRE(std::isfinite(out[i]));
        e += double(out[i]) * out[i];
    }
    CHECK(e > 1e-6);
}

TEST_CASE("nam: an LSTM model loads and a missing or invalid file is reported, not thrown") {
    std::string err;
    CHECK(NamModel::load(kModels + "/lstm.nam", 48000.0, 256, err) != nullptr);
    CHECK(NamModel::load(kModels + "/does_not_exist.nam", 48000.0, 256, err) == nullptr);
    CHECK(!err.empty());
    CHECK(NamModel::load(SAS_NAM_EXAMPLES_DIR "/../README.md", 48000.0, 256, err) == nullptr);
    CHECK(!err.empty());
}

TEST_CASE("nam: processing in one block or in small blocks gives the same output") {
    std::string err;
    auto a = NamModel::load(kModels + "/wavenet.nam", 48000.0, 512, err);
    auto b = NamModel::load(kModels + "/wavenet.nam", 48000.0, 512, err);
    REQUIRE(a);
    REQUIRE(b);
    const auto in = sine(330.0, 0.3, 2048, 48000.0);
    std::vector<float> oa(in.size()), ob(in.size());
    a->process(in.data(), oa.data(), in.size());
    for (std::size_t at = 0; at < in.size(); at += 64) b->process(in.data() + at, ob.data() + at, 64);
    for (std::size_t i = 0; i < in.size(); ++i) REQUIRE(std::fabs(oa[i] - ob[i]) < 1e-5f);
}

#include "sas/chain.h"
#include "sas/embedded_amps.h"

TEST_CASE("nam: the loader is an amp of its own that the circuit lists do not include") {
    const auto circuits = embeddedAmpIds();
    const auto all = embeddedAmpIds(true);
    CHECK(all.size() == circuits.size() + 1);
    CHECK(std::find(circuits.begin(), circuits.end(), "nam_loader") == circuits.end());
    CHECK(all.back() == "nam_loader");
    const AmpDef def = loadEmbeddedAmp("nam_loader");
    CHECK(def.nam);
    CHECK(def.preamp.knobs.empty());
}

TEST_CASE("nam: the chain with the loader amp runs the model, passes the signal without one, and has no latency") {
    const AmpDef def = loadEmbeddedAmp("nam_loader");
    std::string err;
    auto model = NamModel::load(kModels + "/wavenet.nam", 48000.0, 512, err);
    REQUIRE(model);
    Chain chain(def, 48000.0);
    CHECK(chain.latencySamples() == 0.0);
    ChainSettings s;
    s.cabFilter = false;
    const auto in = sine(220.0, 0.2, 9600, 48000.0);
    auto run = [&](NamModel* m) {
        s.nam = m;
        chain.snap(s);
        std::vector<float> out(in.size());
        chain.processAmp(in.data(), out.data(), in.size());
        chain.applyOutput(out.data(), out.size());
        double e = 0.0;
        for (std::size_t i = 4800; i < out.size(); ++i) {
            REQUIRE(std::isfinite(out[i]));
            e += double(out[i]) * out[i];
        }
        return e;
    };
    const double without = run(nullptr), with = run(model.get());
    CHECK(without > 1e-4);
    CHECK(with > 1e-4);
    CHECK(with != without);  // the model changes the sound
    s.namLevelDb = -12.0;
    CHECK(run(model.get()) < with);
}

TEST_CASE("nam: the tone control after the model is flat at the middle and moves the bass and the treble") {
    const AmpDef def = loadEmbeddedAmp("nam_loader");
    std::string err;
    auto model = NamModel::load(kModels + "/wavenet.nam", 48000.0, 512, err);
    REQUIRE(model);
    auto level = [&](double hz, double bass, double mid, double treble) {
        Chain chain(def, 48000.0);
        ChainSettings s;
        s.cabFilter = false;
        s.nam = model.get();
        s.namBass = bass;
        s.namMid = mid;
        s.namTreble = treble;
        chain.snap(s);
        const auto in = sine(hz, 0.1, 19200, 48000.0);
        std::vector<float> out(in.size());
        chain.processAmp(in.data(), out.data(), in.size());
        double e = 0.0;
        for (std::size_t i = 9600; i < out.size(); ++i) e += double(out[i]) * out[i];
        return 10.0 * std::log10(e / double(out.size() - 9600) + 1e-30);
    };
    // The same model gives the same level whatever the tone knobs do at a frequency they do not touch, and the knobs move their own band.
    const double flat100 = level(100.0, 0.5, 0.5, 0.5), flat5k = level(5000.0, 0.5, 0.5, 0.5);
    CHECK(level(100.0, 1.0, 0.5, 0.5) > flat100 + 3.0);
    CHECK(level(100.0, 0.0, 0.5, 0.5) < flat100 - 3.0);
    CHECK(level(5000.0, 0.5, 0.5, 1.0) > flat5k + 3.0);
    CHECK(level(5000.0, 0.5, 0.5, 0.0) < flat5k - 3.0);
}
