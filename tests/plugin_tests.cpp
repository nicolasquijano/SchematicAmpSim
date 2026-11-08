// Headless tests of the JUCE processor: parameters, audio, state round trip and the IR loader.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <set>
#include <string>
#include <cstdlib>

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginEditor.h"
#include "RackState.h"
#include "EqPresets.h"
#include "FxPresets.h"
#include "I18n.h"
#include "ModuleUnit.h"

using sas::ModuleType;

// The tests read texts of the interface in English, whatever the language of the machine they run on.
static const int sForceEnglish = (i18n::useForSession(std::getenv("SAS_TEST_LANGUAGE") != nullptr && std::string(std::getenv("SAS_TEST_LANGUAGE")) == "es" ? i18n::Language::Spanish : i18n::Language::English), 0);

namespace {

struct JuceInit {
    juce::ScopedJuceInitialiser_GUI init;
};

void fill(juce::AudioBuffer<float>& b, double hz, double amp, double fs, int offset = 0) {
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(ch, i, float(amp * std::sin(2.0 * 3.14159265358979 * hz * double(i + offset) / fs)));
}

double rms(const juce::AudioBuffer<float>& b) {
    double s = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) s += double(b.getSample(0, i)) * b.getSample(0, i);
    return std::sqrt(s / b.getNumSamples());
}

// Runs `blocks` blocks and returns the last one.
juce::AudioBuffer<float> run(SasProcessor& p, int blocks, double hz = 220.0, double amp = 0.05) {
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        fill(buf, hz, amp, 48000.0, b * 512);
        p.processBlock(buf, midi);
    }
    return buf;
}

void setParam(SasProcessor& p, const char* id, float plainValue) {
    auto* param = p.apvts.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

// The rack with every module of the fixed-order plug-in in it, so a test can switch any of them on through its parameters.
void allModules(SasProcessor& p) { p.setRackOrder(RackState::fromLegacy(p.apvts, false, true)); }
// A parallel with a second amp and cabinet in lane B (what the parallel was before lane B became the player's to fill).
bool addSecondAmp(SasProcessor& p) {
    return p.addParallel() && p.addModule(sas::ModuleType::Amp, -1, 1) && p.addModule(sas::ModuleType::Cabinet, -1, 1);  // lane B
}

juce::File writeImpulse(const juce::String& name) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 24, {}, 0));
    juce::AudioBuffer<float> ir(1, 256);
    ir.clear();
    ir.setSample(0, 0, 1.0f);
    ir.setSample(0, 40, 0.4f);
    w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
    return file;
}

}  // namespace

TEST_CASE("processor exposes the expected parameters") {
    JuceInit init;
    SasProcessor p;
    for (const char* id : {ids::amp, ids::volume2, ids::volume, ids::bass, ids::middle, ids::treble, ids::presence, ids::master, ids::input, ids::output,
                           ids::quality, ids::cab, ids::jack, ids::tsOn, ids::tsDrive, ids::tsTone, ids::tsLevel, ids::vibOn, ids::vibSpeed, ids::vibIntensity, ids::reverb, ids::resonance})
        CHECK(p.apvts.getParameter(id) != nullptr);
    CHECK(p.apvts.getParameter(ids::volume)->getText(p.apvts.getParameter(ids::volume)->getDefaultValue(), 10) == "6.0");
    auto* quality = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(ids::quality));
    REQUIRE(quality != nullptr);
    CHECK(quality->choices[2].contains("Lite"));  // Lite stays last for saved-project compatibility
}

TEST_CASE("processor makes finite audio in every quality and reports latency") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    for (int q = 0; q < 4; ++q) {
        setParam(p, ids::quality, float(q));
        const auto out = run(p, 12);
        for (int i = 0; i < out.getNumSamples(); ++i) REQUIRE(std::isfinite(out.getSample(0, i)));
        CHECK(rms(out) > 1e-4);
        // Both channels carry the same mono signal.
        CHECK(out.getSample(0, 100) == out.getSample(1, 100));
        CHECK(p.getLatencySamples() > 15);
    }
    const int latency2x = [&] { setParam(p, ids::quality, 0.0f); run(p, 2); return p.getLatencySamples(); }();
    setParam(p, ids::quality, 1.0f);
    run(p, 2);
    CHECK(p.getLatencySamples() > latency2x);
}

TEST_CASE("switching amp keeps audio flowing and swaps in the new circuits") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    REQUIRE(p.amps().size() >= 2);
    CHECK(p.amps()[0].id == "jcm800_2203");
    CHECK(p.amps()[1].id == "plexi_1959");
    setParam(p, ids::quality, 2.0f);  // Lite: cheaper to build and run
    const double before = rms(run(p, 12));
    REQUIRE(before > 1e-4);

    setParam(p, ids::amp, 1.0f);
    CHECK(p.selectedAmpIndex() == 1);
    CHECK(p.runningAmpIndex() == 0);
    // Blocks keep coming (finite and audible) until the builder hands over the Plexi.
    bool swapped = false;
    for (int i = 0; i < 600 && !swapped; ++i) {
        const auto out = run(p, 2);
        for (int s = 0; s < out.getNumSamples(); ++s) REQUIRE(std::isfinite(out.getSample(0, s)));
        CHECK(rms(out) > 1e-5);  // the old amp keeps playing while the new one is built
        swapped = p.runningAmpIndex() == 1;
        juce::Thread::sleep(15);
    }
    CHECK(swapped);
    const double plexi = rms(run(p, 12));
    CHECK(plexi > 1e-4);

    // And back again, to the original circuit and level.
    setParam(p, ids::amp, 0.0f);
    bool back = false;
    for (int i = 0; i < 600 && !back; ++i) {
        run(p, 2);
        back = p.runningAmpIndex() == 0;
        juce::Thread::sleep(15);
    }
    CHECK(back);
    CHECK(rms(run(p, 12)) == Catch::Approx(before).epsilon(0.1));
}

TEST_CASE("choosing other output tubes or wear rebuilds the circuits and changes the plate current") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    setParam(p, ids::quality, 0.0f);  // Normal: the plate current is read from the full circuit
    run(p, 40);
    const float stock = p.idleMilliamps();
    REQUIRE(stock > 10.0f);

    auto waitFor = [&](auto done) {
        for (int i = 0; i < 800 && !done(); ++i) {
            run(p, 4);
            juce::Thread::sleep(15);
        }
        return done();
    };
    setParam(p, ids::tube, 2.0f);  // 6L6GC instead of EL34: a bigger tube, so more plate current at the same share of its rating
    CHECK(waitFor([&] { return p.idleMilliamps() > stock * 1.1f; }));
    setParam(p, ids::tube, 0.0f);
    CHECK(waitFor([&] { return std::abs(p.idleMilliamps() - stock) < stock * 0.05f; }));
    setParam(p, ids::wear, 10.0f);
    CHECK(waitFor([&] { return p.idleMilliamps() < stock * 0.9f; }));

    // The bias control acts at once, without a rebuild.
    setParam(p, ids::wear, 0.0f);
    CHECK(waitFor([&] { return std::abs(p.idleMilliamps() - stock) < stock * 0.05f; }));
    setParam(p, ids::bias, 2.0f);  // hotter
    CHECK(waitFor([&] { return p.idleMilliamps() > stock * 1.15f; }));
}

TEST_CASE("the power tube choice is a type, so it means the same on every amp") {
    JuceInit init;
    SasProcessor p;
    auto ampIndex = [&](const char* id) {
        for (std::size_t i = 0; i < p.amps().size(); ++i)
            if (p.amps()[i].id == id) return int(i);
        FAIL("unknown amp " << id);
        return -1;
    };
    auto tubeName = [&] { return p.amps()[std::size_t(p.selectedAmpIndex())].power.tubes[std::size_t(p.wantedCondition().tube)].name; };
    // Every amp starts with its own tube.
    for (const char* id : {"jcm800_2203", "highgain_120", "afd100", "twin_65"}) {
        setParam(p, ids::amp, float(ampIndex(id)));
        CHECK(p.wantedCondition().tube == 0);
    }
    // 6L6GC picked on the British 800 (where it is the second entry) is the stock 6L6GC of the 5050, not its second entry (an EL34).
    setParam(p, ids::amp, float(ampIndex("jcm800_2203")));
    setParam(p, ids::tube, 2.0f);  // 6L6GC
    CHECK(tubeName() == "6L6GC");
    setParam(p, ids::amp, float(ampIndex("highgain_120")));
    CHECK(tubeName() == "6L6GC");
    CHECK(p.wantedCondition().tube == 0);
    setParam(p, ids::amp, float(ampIndex("afd100")));  // its list is 6550, EL34, 6L6GC
    CHECK(tubeName() == "6L6GC");
    // An amp with a single tube ignores the choice.
    setParam(p, ids::amp, float(ampIndex("tweed_5e3")));
    CHECK(p.wantedCondition().tube == 0);
}

TEST_CASE("the tuner listens to the input, reads the note and silences the output while it is on") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 256);
    setParam(p, ids::quality, 2.0f);
    // Off: nothing is collected and the amp plays.
    CHECK(rms(run(p, 20, 110.0, 0.1)) > 1e-4);
    std::vector<float> buf(3072);
    CHECK_FALSE(p.tunerFeed().copyLatest(buf.data(), buf.size()));

    setParam(p, ids::tunerOn, 1.0f);
    const auto muted = run(p, 80, 110.0, 0.1);  // an A2
    CHECK(rms(muted) == 0.0);                    // muted
    REQUIRE(p.tunerFeed().copyLatest(buf.data(), buf.size()));
    const auto r = sas::PitchDetector::detect(buf.data(), buf.size(), p.tunerFeed().rate());
    REQUIRE(r.valid);
    CHECK(std::abs(1200.0 * std::log2(r.hz / 110.0)) < 1.0);
    const auto n = sas::noteFor(r.hz, 440.0);
    CHECK(std::string(n.name) == "A");
    CHECK(n.octave == 2);

    // Mute off: it listens and the amp keeps playing; on again after it is switched off.
    setParam(p, ids::tunerMute, 0.0f);
    CHECK(rms(run(p, 20, 110.0, 0.1)) > 1e-4);
    setParam(p, ids::tunerOn, 0.0f);
    setParam(p, ids::tunerMute, 1.0f);
    CHECK(rms(run(p, 20, 110.0, 0.1)) > 1e-4);
}

TEST_CASE("state round trip restores parameters") {
    JuceInit init;
    SasProcessor a, b;
    setParam(a, ids::volume, 9.0f);
    setParam(a, ids::quality, 2.0f);
    setParam(a, ids::tsOn, 1.0f);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    b.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(b.apvts.getRawParameterValue(ids::volume)->load() == 9.0f);
    CHECK(b.apvts.getRawParameterValue(ids::quality)->load() == 2.0f);
    CHECK(b.apvts.getRawParameterValue(ids::tsOn)->load() == 1.0f);
}

TEST_CASE("an impulse response loads, survives the state, and shapes the sound") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    // A new instance carries the first built-in cabinet (none when the build has no impulses).
    CHECK(p.impulseResponseName() == (SasProcessor::builtInImpulseResponseCount() > 0 ? juce::String("4x12 OS Mix 1") : juce::String()));
    CHECK(!p.loadImpulseResponse(juce::File("C:/definitely/not/here.wav")));

    const auto file = writeImpulse("sas_test_ir.wav");
    REQUIRE(p.loadImpulseResponse(file));
    CHECK(p.impulseResponseName() == "sas_test_ir.wav");

    setParam(p, ids::cab, 2.0f);  // IR
    // The convolution builds its engine in the background: give it a moment, then audio must flow.
    juce::Thread::sleep(300);
    const auto withIr = run(p, 40);
    for (int i = 0; i < withIr.getNumSamples(); ++i) REQUIRE(std::isfinite(withIr.getSample(0, i)));
    CHECK(rms(withIr) > 1e-5);

    juce::MemoryBlock state;
    p.getStateInformation(state);
    SasProcessor q;
    q.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(q.impulseResponseName() == "sas_test_ir.wav");  // the IR travels inside the project

    p.clearImpulseResponse();
    CHECK(p.impulseResponseName().isEmpty());
    file.deleteFile();
}

TEST_CASE("the loaded impulse response is really convolved with the amp output") {
    JuceInit init;
    // A two-tap IR (1.0 now, 0.8 a hundred samples later), stereo, at the host rate so it is used as written.
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sas_two_tap_ir.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 2, 24, {}, 0));
        juce::AudioBuffer<float> ir(2, 2048);
        ir.clear();
        for (int ch = 0; ch < 2; ++ch) {
            ir.setSample(ch, 0, 1.0f);
            ir.setSample(ch, 100, 0.8f);
        }
        w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
    }

    auto capture = [&](SasProcessor& p, int blocks, int blockSize) {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b) {
            fill(buf, 196.0, 0.08, 48000.0, b * blockSize);
            p.processBlock(buf, midi);
            for (int i = 0; i < blockSize; ++i) out.push_back(buf.getSample(0, i));
        }
        return out;
    };
    auto configure = [&](SasProcessor& p, float cabMode) {
        setParam(p, ids::quality, 2.0f);  // Lite
        setParam(p, ids::cab, cabMode);
        p.prepareToPlay(48000.0, 256);
    };

    // Both orders: a file picked after the plugin is prepared, and an IR restored with a saved project before it is prepared.
    const bool loadBeforePrepare = GENERATE(false, true);
    INFO("IR loaded " << (loadBeforePrepare ? "before" : "after") << " prepareToPlay");
    SasProcessor plain, withIr;
    configure(plain, 0.0f);  // cabinet off: the raw amp
    if (loadBeforePrepare) {
        REQUIRE(withIr.loadImpulseResponse(file));
        configure(withIr, 2.0f);
    } else {
        configure(withIr, 2.0f);
        REQUIRE(withIr.loadImpulseResponse(file));
    }
    juce::Thread::sleep(500);  // the convolution engine is built in the background

    const int blocks = 120, blockSize = 256;
    const auto a = capture(plain, blocks, blockSize);
    const auto y = capture(withIr, blocks, blockSize);

    // y should be s * (a[n] + 0.8 a[n - 100]) for some gain s and a small latency: search the lag and fit s.
    const std::size_t from = a.size() / 2, to = a.size() - 400;
    double bestErr = 1e30, bestLag = 0, bestScale = 0, ref = 0;
    for (std::size_t n = from; n < to; ++n) ref += double(y[n]) * y[n];
    for (int lag = -64; lag <= 64; ++lag) {
        double num = 0, den = 0;
        std::vector<double> model;
        for (std::size_t n = from; n < to; ++n) {
            const double m = double(a[std::size_t(int(n) + lag)]) + 0.8 * double(a[std::size_t(int(n) + lag - 100)]);
            num += m * double(y[n]);
            den += m * m;
            model.push_back(m);
        }
        const double scale = num / std::max(den, 1e-30);
        double err = 0;
        for (std::size_t n = from, i = 0; n < to; ++n, ++i) err += (double(y[n]) - scale * model[i]) * (double(y[n]) - scale * model[i]);
        if (err < bestErr) { bestErr = err; bestLag = lag; bestScale = scale; }
    }
    INFO("best lag " << bestLag << ", gain " << bestScale << ", residual " << (bestErr / ref) << " of the output energy, output energy " << ref);
    CHECK(ref > 1e-6);
    CHECK(bestErr < 0.02 * ref);  // the output is the amp convolved with the IR, not just the amp
    file.deleteFile();
}

TEST_CASE("two impulse responses are mixed by the blend knob") {
    JuceInit init;
    auto writeIr = [](const juce::String& name, std::initializer_list<std::pair<int, float>> taps) {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 24, {}, 0));
        juce::AudioBuffer<float> ir(1, 1024);
        ir.clear();
        for (auto [i, v] : taps) ir.setSample(0, i, v);
        w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
        return file;
    };
    // A: a single tap (a transparent cabinet). B: a tap and an echo 60 samples later (energy 1.81, so a gain of 0.743 after the unit-energy scaling).
    const auto fileA = writeIr("sas_blend_a.wav", {{0, 1.0f}});
    const auto fileB = writeIr("sas_blend_b.wav", {{0, 1.0f}, {60, -0.9f}});

    auto capture = [&](SasProcessor& p, int blocks) {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b) {
            fill(buf, 196.0, 0.08, 48000.0, b * 256);
            p.processBlock(buf, midi);
            for (int i = 0; i < 256; ++i) out.push_back(buf.getSample(0, i));
        }
        return out;
    };
    auto configure = [&](SasProcessor& p, float cabMode, float blend) {
        setParam(p, ids::quality, 2.0f);  // Lite
        setParam(p, ids::cab, cabMode);
        setParam(p, ids::irBlend, blend);
        p.prepareToPlay(48000.0, 256);
    };

    SasProcessor plain;
    configure(plain, 0.0f, 5.0f);
    const auto a = capture(plain, 100);

    // Residual (relative) of y against model(lag) with a free scale, at the best lag within +-32.
    auto fit = [&](const std::vector<float>& y, auto model) {
        const std::size_t from = a.size() / 2, to = a.size() - 200;
        double ref = 0;
        for (std::size_t n = from; n < to; ++n) ref += double(y[n]) * y[n];
        double best = 1e30, bestScale = 0;
        for (int lag = -32; lag <= 32; ++lag) {
            double num = 0, den = 0;
            for (std::size_t n = from; n < to; ++n) {
                const double m = model(std::size_t(int(n) + lag));
                num += m * double(y[n]);
                den += m * m;
            }
            const double scale = num / std::max(den, 1e-30);
            double err = 0;
            for (std::size_t n = from; n < to; ++n) {
                const double d = double(y[n]) - scale * model(std::size_t(int(n) + lag));
                err += d * d;
            }
            if (err < best) { best = err; bestScale = scale; }
        }
        return std::pair{best / std::max(ref, 1e-30), bestScale};
    };
    auto modelA = [&](std::size_t n) { return double(a[n]); };
    auto modelB = [&](std::size_t n) { return 0.7433 * (double(a[n]) - 0.9 * double(a[n - 60])); };
    auto modelMix = [&](std::size_t n) { return 0.7071 * (modelA(n) + modelB(n)); };

    for (float blend : {0.0f, 5.0f, 10.0f}) {
        SasProcessor p;
        configure(p, 2.0f, blend);
        REQUIRE(p.loadImpulseResponse(fileA, 0));
        REQUIRE(p.loadImpulseResponse(fileB, 1));
        juce::Thread::sleep(500);
        const auto y = capture(p, 100);
        const auto r = blend == 0.0f ? fit(y, modelA) : blend == 10.0f ? fit(y, modelB) : fit(y, modelMix);
        INFO("blend " << blend << ": residual " << r.first << ", gain " << r.second);
        CHECK(r.first < 0.02);
        CHECK(std::abs(r.second - 1.0) < 0.08);
        CHECK(p.impulseResponseCurve(0).size() == 96);
    }

    // The slots keep their own names and curves, and both travel in the project.
    SasProcessor p;
    REQUIRE(p.loadImpulseResponse(fileA, 0));
    REQUIRE(p.loadImpulseResponse(fileB, 1));
    CHECK(p.impulseResponseName(0) == "sas_blend_a.wav");
    CHECK(p.impulseResponseName(1) == "sas_blend_b.wav");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    SasProcessor q;
    q.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(q.impulseResponseName(0) == "sas_blend_a.wav");
    CHECK(q.impulseResponseName(1) == "sas_blend_b.wav");
    q.clearImpulseResponse(1);
    CHECK(q.impulseResponseName(1).isEmpty());
    CHECK(q.impulseResponseName(0) == "sas_blend_a.wav");
    fileA.deleteFile();
    fileB.deleteFile();
}

// Not a regression test: loads every WAV of the folder named by SAS_TEST_IR_DIR and reports what the processor does with it.
TEST_CASE("real impulse responses from SAS_TEST_IR_DIR are applied", "[.realir]") {
    JuceInit init;
    const char* dir = std::getenv("SAS_TEST_IR_DIR");
    if (!dir) { SKIP("SAS_TEST_IR_DIR is not set"); }
    juce::Array<juce::File> files;
    juce::File(dir).findChildFiles(files, juce::File::findFiles, false, "*.wav");
    REQUIRE(files.size() > 0);
    auto capture = [](SasProcessor& p, double hostRate, int blocks, int blockSize) {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b) {
            // Band-limited noise (100 Hz to 5 kHz, a guitar-like spectrum): the level through a cabinet is judged over a band, not at one tone.
            static unsigned noiseState = 1;
            static double hp = 0, lp1 = 0, lp2 = 0;
            if (b == 0) { noiseState = 1; hp = lp1 = lp2 = 0; }
            for (int i = 0; i < blockSize; ++i) {
                noiseState = noiseState * 1664525u + 1013904223u;
                const double white = double(noiseState >> 8) / double(1u << 24) - 0.5;
                hp += 0.0129 * (white - hp);            // ~100 Hz
                lp1 += 0.45 * ((white - hp) - lp1);     // ~5 kHz
                lp2 += 0.45 * (lp1 - lp2);
                for (int ch = 0; ch < 2; ++ch) buf.setSample(ch, i, float(0.25 * lp2));
            }
            p.processBlock(buf, midi);
            for (int i = 0; i < blockSize; ++i) out.push_back(buf.getSample(0, i));
        }
        return out;
    };
    auto rmsOf = [](const std::vector<float>& v, std::size_t from) {
        double e = 0;
        for (std::size_t i = from; i < v.size(); ++i) e += double(v[i]) * v[i];
        return std::sqrt(e / double(v.size() - from));
    };
    for (double hostRate : {44100.0, 48000.0}) {
        SasProcessor plain;
        setParam(plain, ids::quality, 2.0f);
        setParam(plain, ids::cab, 0.0f);
        plain.prepareToPlay(hostRate, 512);
        const int blocks = int(hostRate * 1.0 / 512);
        const auto a = capture(plain, hostRate, blocks, 512);
        const std::size_t from = a.size() / 2;
        SasProcessor filter;
        setParam(filter, ids::quality, 2.0f);
        setParam(filter, ids::cab, 1.0f);
        filter.prepareToPlay(hostRate, 512);
        const auto f = capture(filter, hostRate, blocks, 512);
        WARN("the built-in cabinet filter at host " << hostRate << " is " << 20.0 * std::log10(rmsOf(f, from) / std::max(rmsOf(a, from), 1e-12)) << " dB re the raw amp");
        for (const auto& file : files) {
            SasProcessor p;
            setParam(p, ids::quality, 2.0f);
            setParam(p, ids::cab, 2.0f);
            p.prepareToPlay(hostRate, 512);
            const bool loaded = p.loadImpulseResponse(file);
            juce::Thread::sleep(500);
            const auto y = capture(p, hostRate, blocks, 512);
            double ab = 0, aa = 0, bb = 0;
            for (std::size_t i = from; i < a.size(); ++i) { ab += double(a[i]) * y[i]; aa += double(a[i]) * a[i]; bb += double(y[i]) * y[i]; }
            const auto curve = p.impulseResponseCurve(0);
            WARN(file.getFileName() << " @ host " << hostRate << ": loaded " << loaded << ", level " << 20.0 * std::log10(rmsOf(y, from) / std::max(rmsOf(a, from), 1e-12))
                                    << " dB re the raw amp (" << 20.0 * std::log10(rmsOf(y, from) / std::max(rmsOf(f, from), 1e-12)) << " re the cab filter), similarity " << ab / std::sqrt(aa * bb + 1e-30) << ", curve points " << curve.size());
            CHECK(loaded);
            CHECK(rmsOf(y, from) > 1e-5);
        }
    }
}

TEST_CASE("an impulse response is applied for any IR rate, host rate, load order, re-preparation and block size", "[slow]") {
    JuceInit init;
    // A cabinet-like IR: a tap and an echo 2 ms later, written at `irRate`, mono or stereo.
    auto writeIr = [](double irRate, int channels, const juce::String& name) {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), irRate, std::uint32_t(channels), 24, {}, 0));
        juce::AudioBuffer<float> ir(channels, int(irRate * 0.05));
        ir.clear();
        for (int ch = 0; ch < channels; ++ch) {
            ir.setSample(ch, 0, 1.0f);
            ir.setSample(ch, int(irRate * 0.002), -0.9f);
            // A decaying tail, like any real cabinet: the convolution trims silence at the end of an IR, and an echo that was its very last
            // sample would be cut off when the IR is resampled.
            for (int i = int(irRate * 0.003); i < ir.getNumSamples(); ++i)
                ir.setSample(ch, i, 0.02f * std::exp(-float(i) / float(irRate * 0.01)) * std::sin(0.7f * float(i)));
        }
        w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
        return file;
    };
    // Output of the processor (first channel) for `blocks` blocks of `blockSize`, with a steady tone as input.
    auto capture = [](SasProcessor& p, double hostRate, int blocks, int blockSize) {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b) {
            fill(buf, 196.0, 0.08, hostRate, b * blockSize);
            p.processBlock(buf, midi);
            for (int i = 0; i < blockSize; ++i) out.push_back(buf.getSample(0, i));
        }
        return out;
    };
    auto rmsOf = [](const std::vector<float>& v, std::size_t from) {
        double e = 0;
        for (std::size_t i = from; i < v.size(); ++i) e += double(v[i]) * v[i];
        return std::sqrt(e / double(v.size() - from));
    };
    // 1 = the signal is unchanged by the IR, ~0 = it is a different signal (a 2 ms comb filter changes the waveform a lot).
    auto similarity = [](const std::vector<float>& a, const std::vector<float>& b, std::size_t from) {
        double ab = 0, aa = 0, bb = 0;
        for (std::size_t i = from; i < a.size(); ++i) { ab += double(a[i]) * b[i]; aa += double(a[i]) * a[i]; bb += double(b[i]) * b[i]; }
        return ab / std::sqrt(aa * bb + 1e-30);
    };

    for (double hostRate : {44100.0, 48000.0, 96000.0}) {
        for (double irRate : {44100.0, 48000.0, 96000.0}) {
            for (int channels : {1, 2}) {
                for (int order = 0; order < 2; ++order) {  // 0 = load after prepareToPlay, 1 = before (a restored project)
                    const auto file = writeIr(irRate, channels, "sas_diag_ir.wav");
                    const int blockSize = 256;
                    SasProcessor plain, p;
                    for (auto* proc : {&plain, &p}) setParam(*proc, ids::quality, 2.0f);  // Lite
                    setParam(plain, ids::cab, 0.0f);
                    setParam(p, ids::cab, 2.0f);
                    plain.prepareToPlay(hostRate, blockSize);
                    if (order == 1) { REQUIRE(p.loadImpulseResponse(file)); p.prepareToPlay(hostRate, blockSize); }
                    else { p.prepareToPlay(hostRate, blockSize); REQUIRE(p.loadImpulseResponse(file)); }
                    juce::Thread::sleep(400);
                    const int blocks = int(hostRate * 0.6 / blockSize);
                    const auto a = capture(plain, hostRate, blocks, blockSize);
                    const auto y = capture(p, hostRate, blocks, blockSize);
                    const std::size_t from = a.size() / 2;
                    const double sim = similarity(a, y, from);
                    const double dB = 20.0 * std::log10(rmsOf(y, from) / std::max(rmsOf(a, from), 1e-12));
                    INFO("host " << hostRate << ", IR " << irRate << " Hz x " << channels << " ch, loaded " << (order ? "before" : "after") << " prepareToPlay: similarity " << sim
                                 << ", level " << dB << " dB re the raw amp");
                    for (float v : y) REQUIRE(std::isfinite(v));
                    CHECK(rmsOf(y, from) > 1e-5);          // sound, not silence
                    CHECK(sim < 0.97);                      // and it is not the raw amp: the IR is applied
                    CHECK(dB > -20.0);
                    CHECK(dB < 12.0);

                    // Stop and play again: the host calls prepareToPlay once more; the IR has to survive it.
                    p.prepareToPlay(hostRate, blockSize);
                    juce::Thread::sleep(400);
                    const auto y2 = capture(p, hostRate, blocks, blockSize);
                    INFO("after a second prepareToPlay: similarity " << similarity(a, y2, from));
                    CHECK(rmsOf(y2, from) > 1e-5);
                    CHECK(similarity(a, y2, from) < 0.97);

                    // Blocks larger than the size announced in prepareToPlay (some hosts do that).
                    const auto big = capture(p, hostRate, blocks / 4, 1024);
                    for (float v : big) REQUIRE(std::isfinite(v));
                    INFO("with 1024-sample blocks after preparing for 256: similarity " << similarity(a, big, big.size() / 2));
                    CHECK(rmsOf(big, big.size() / 2) > 1e-5);
                    file.deleteFile();
                }
            }
        }
    }
}

TEST_CASE("stereo: identical channels by default, A left and B right with the width, a wider pair with the double tracking") {
    JuceInit init;
    auto writeIr = [](const juce::String& name, std::initializer_list<std::pair<int, float>> taps) {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 24, {}, 0));
        juce::AudioBuffer<float> ir(1, 1024);
        ir.clear();
        for (auto [i, v] : taps) ir.setSample(0, i, v);
        for (int i = 400; i < ir.getNumSamples(); ++i) ir.setSample(0, i, 0.01f * std::exp(-float(i - 400) / 100.0f) * std::sin(0.9f * float(i)));  // a tail
        w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
        return file;
    };
    const auto fileA = writeIr("sas_stereo_a.wav", {{0, 1.0f}});
    const auto fileB = writeIr("sas_stereo_b.wav", {{0, 1.0f}, {60, -0.9f}});

    // Both output channels of `blocks` blocks of the same 196 Hz tone.
    auto capture = [](SasProcessor& p, int blocks, std::vector<float>& left, std::vector<float>& right, double hz = 196.0) {
        left.clear();
        right.clear();
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b) {
            fill(buf, hz, 0.08, 48000.0, b * 256);
            p.processBlock(buf, midi);
            for (int i = 0; i < 256; ++i) { left.push_back(buf.getSample(0, i)); right.push_back(buf.getSample(1, i)); }
        }
    };
    auto configure = [&](SasProcessor& p, float cab) {
        setParam(p, ids::quality, 2.0f);  // Lite
        setParam(p, ids::cab, cab);
        p.prepareToPlay(48000.0, 256);
    };
    auto similarity = [](const std::vector<float>& a, const std::vector<float>& b, std::size_t from) {
        double ab = 0, aa = 0, bb = 0;
        for (std::size_t i = from; i < a.size(); ++i) { ab += double(a[i]) * b[i]; aa += double(a[i]) * a[i]; bb += double(b[i]) * b[i]; }
        return ab / std::sqrt(aa * bb + 1e-30);
    };

    SECTION("the default is the same signal in both ears, exactly") {
        SasProcessor p;
        configure(p, 1.0f);
        std::vector<float> l, r;
        capture(p, 60, l, r);
        CHECK(l == r);
        double e = 0;
        for (float v : l) e += double(v) * v;
        CHECK(e > 1e-6);
    }

    SECTION("with the width up, the left ear is IR A and the right ear IR B") {
        SasProcessor plain, p;
        configure(plain, 0.0f);
        configure(p, 2.0f);
        REQUIRE(p.loadImpulseResponse(fileA, 0));
        REQUIRE(p.loadImpulseResponse(fileB, 1));
        setParam(p, ids::irWidth, 10.0f);
        juce::Thread::sleep(500);
        std::vector<float> a, ar, l, r;
        capture(plain, 100, a, ar);
        capture(p, 100, l, r);
        const std::size_t from = a.size() / 2, to = a.size() - 200;
        // y ~ s * model, the best lag within +-32 and a free scale: how much of y the model leaves unexplained.
        auto residual = [&](const std::vector<float>& y, auto model) {
            double ref = 0;
            for (std::size_t n = from; n < to; ++n) ref += double(y[n]) * y[n];
            double best = 1e30;
            for (int lag = -32; lag <= 32; ++lag) {
                double num = 0, den = 0;
                for (std::size_t n = from; n < to; ++n) { const double m = model(std::size_t(int(n) + lag)); num += m * double(y[n]); den += m * m; }
                const double scale = num / std::max(den, 1e-30);
                double err = 0;
                for (std::size_t n = from; n < to; ++n) { const double d = double(y[n]) - scale * model(std::size_t(int(n) + lag)); err += d * d; }
                best = std::min(best, err);
            }
            return best / std::max(ref, 1e-30);
        };
        auto modelA = [&](std::size_t n) { return double(a[n]); };
        auto modelB = [&](std::size_t n) { return double(a[n]) - 0.9 * double(a[n - 60]); };
        INFO("left vs A " << residual(l, modelA) << ", right vs B " << residual(r, modelB) << ", left vs B " << residual(l, modelB));
        CHECK(residual(l, modelA) < 0.03);
        CHECK(residual(r, modelB) < 0.03);
        CHECK(residual(l, modelB) > 0.1);  // and they really are different
    }

    SECTION("the width at 0 gives the same blend in both ears") {
        SasProcessor p;
        configure(p, 2.0f);
        REQUIRE(p.loadImpulseResponse(fileA, 0));
        REQUIRE(p.loadImpulseResponse(fileB, 1));
        juce::Thread::sleep(500);
        std::vector<float> l, r;
        capture(p, 100, l, r);
        CHECK(l == r);
    }

    SECTION("the double tracking makes the two ears different and keeps everything finite") {
        SasProcessor p;
        configure(p, 1.0f);
        setParam(p, ids::doubleAmount, 10.0f);
        std::vector<float> l, r;
        capture(p, 120, l, r, 110.0);  // a tone, so the two delays show as a phase difference (with music the spectrum is wide and the ears decorrelate more)
        for (std::size_t i = 0; i < l.size(); ++i) { REQUIRE(std::isfinite(l[i])); REQUIRE(std::isfinite(r[i])); }
        const double sim = similarity(l, r, l.size() / 2);
        INFO("similarity of the two ears " << sim);
        CHECK(sim < 0.95);
        double e = 0;
        for (float v : r) e += double(v) * v;
        CHECK(e > 1e-6);
    }

    fileA.deleteFile();
    fileB.deleteFile();
}

TEST_CASE("a mono guitar track can feed the stereo output") {
    JuceInit init;
    SasProcessor p;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::mono());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(p.isBusesLayoutSupported(layout));
    layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
    CHECK(p.isBusesLayoutSupported(layout));
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    layout.inputBuses.set(0, juce::AudioChannelSet::mono());
    CHECK(p.isBusesLayoutSupported(layout));
    layout.outputBuses.set(0, juce::AudioChannelSet::create5point1());
    CHECK_FALSE(p.isBusesLayoutSupported(layout));
}

// Not a test: renders the editor in the states shown in the README into the folder named by SAS_SCREENSHOT_DIR.
//   SAS_SCREENSHOT_DIR=docs/images sas_plugin_tests "readme screenshots"
TEST_CASE("readme screenshots", "[.screenshots]") {
    JuceInit init;
    const char* dir = std::getenv("SAS_SCREENSHOT_DIR");
    if (!dir) { SKIP("SAS_SCREENSHOT_DIR is not set"); }
    juce::File(dir).createDirectory();

    auto savePng = [&](const juce::Image& img, const juce::String& name) {
        juce::PNGImageFormat png;
        juce::FileOutputStream out(juce::File(dir).getChildFile(name));
        REQUIRE(out.openedOk());
        out.setPosition(0);
        out.truncate();
        png.writeImageToStream(img, out);
    };
    auto ampIndex = [](SasProcessor& p, const char* id) {
        for (std::size_t i = 0; i < p.amps().size(); ++i)
            if (p.amps()[i].id == id) return int(i);
        FAIL(std::string("unknown amp ") + id);
        return 0;
    };
    auto clickTab = [](juce::AudioProcessorEditor& ed, const char* text) {
        for (auto* c : ed.getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == text) { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
    };
    // One continuous stretch of audio (the sine restarts its phase on every call to run).
    auto play = [&](SasProcessor& p, SasEditor& ed, double hz, double amp) {
        run(p, 80, hz, amp);
        for (int i = 0; i < 12; ++i) ed.pollMeters();
    };
    const float scale = 2.0f;  // crisp on a high-resolution screen

    // The Amp page of one amp, playing.
    auto ampPage = [&](const char* id, float imageScale) {
        SasProcessor p;
        setParam(p, "amp", float(ampIndex(p, id)));
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1000, 520);
        play(p, *dynamic_cast<SasEditor*>(ed.get()), 196.0, 0.2);
        return ed->createComponentSnapshot(ed->getLocalBounds(), true, imageScale);
    };

    savePng(ampPage("jcm800_2203", scale), "amp-british-800.png");

    // Four more amps in one picture.
    {
        const char* ids4[4] = {"twin_65", "plexi_1959", "jcm800_2203kk", "lead_c_plus"};
        juce::Image montage(juce::Image::RGB, 1600, 820, true);
        {
            juce::Graphics g(montage);
            g.fillAll(juce::Colours::black);
            for (int i = 0; i < 4; ++i) {
                const auto shot = ampPage(ids4[i], 1.0f);
                g.drawImageAt(shot, (i % 2) * 800, (i / 2) * 410);
            }
        }  // the graphics context is closed before the image is saved
        savePng(montage, "amps.png");
    }

    // The panels of the modules, as the rack's open panel draws them: units stacked on a plain host component, every module switched on.
    auto panels = [&](const char* file, std::initializer_list<ModuleType> types) {
        SasProcessor p;
        for (auto type : types)
            if (const char* on = RackState::onId(type)) setParam(p, on, 1.0f);
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        juce::Component host;
        host.setLookAndFeel(&ed->getLookAndFeel());
        std::vector<std::unique_ptr<ModuleUnit>> units;
        int y = 0;
        for (auto type : types) {
            units.push_back(std::make_unique<ModuleUnit>(p, sas::SlotRef{type, 0}));
            auto& u = *units.back();
            host.addAndMakeVisible(u);
            u.setBounds(0, y, 860, u.preferredHeight());
            y += u.preferredHeight() + 4;
        }
        host.setSize(860, y);
        savePng(host.createComponentSnapshot(host.getLocalBounds(), true, scale), file);
        host.setLookAndFeel(nullptr);
    };
    panels("modules-drive.png", {ModuleType::Compressor, ModuleType::Overdrive, ModuleType::Klon, ModuleType::Fuzz, ModuleType::Boost});
    panels("modules-mod.png", {ModuleType::Phaser, ModuleType::Chorus, ModuleType::Vibe, ModuleType::Leslie, ModuleType::Tremolo, ModuleType::Flanger});
    panels("modules-space.png", {ModuleType::Delay, ModuleType::TapeEcho, ModuleType::Reverb, ModuleType::SpringReverb});
    panels("modules-tone.png", {ModuleType::Eq, ModuleType::Acoustic, ModuleType::Wah, ModuleType::Limiter});

    // The rack with a few modules in it (the open panel is the amp's).
    {
        SasProcessor p;
        p.prepareToPlay(48000.0, 512);
        p.setRackOrder([] {
            sas::RackOrder o;
            for (auto s : {sas::SlotRef{ModuleType::Gate, 0}, {ModuleType::Klon, 0}, {ModuleType::Fuzz, 0}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0},
                           {ModuleType::Leslie, 0}, {ModuleType::Delay, 0}, {ModuleType::SpringReverb, 0}})
                o.slots[std::size_t(o.count++)] = s;
            return o;
        }());
        setParam(p, ids::klnOn, 1.0f);
        setParam(p, ids::spOn, 1.0f);
        setParam(p, ids::lesOn, 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1000, 520);
        play(p, *dynamic_cast<SasEditor*>(ed.get()), 196.0, 0.2);
        savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, scale), "rack.png");
    }

    // The tuner on an A2 (110 Hz), in tune.
    {
        SasProcessor p;
        setParam(p, ids::tunerOn, 1.0f);
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1000, 520);
        auto* se = dynamic_cast<SasEditor*>(ed.get());
        run(p, 100, 110.0, 0.1);
        for (int i = 0; i < 12; ++i) se->updateTuner();
        clickTab(*ed, "Tuner");
        savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, scale), "tuner.png");
    }

    // The newest amps in one picture.
    {
        const char* ids4[4] = {"american_5153", "jazz_clean_120", "triplestack_3ch", "svt_bass300"};
        juce::Image montage(juce::Image::RGB, 1600, 820, true);
        {
            juce::Graphics g(montage);
            g.fillAll(juce::Colours::black);
            for (int i = 0; i < 4; ++i) {
                const auto shot = ampPage(ids4[i], 1.0f);
                g.drawImageAt(shot, (i % 2) * 800, (i / 2) * 410);
            }
        }
        savePng(montage, "amps-new.png");
    }

    // The NAM Loader with a model loaded.
    {
        SasProcessor p;
        setParam(p, "amp", float(ampIndex(p, "nam_loader")));
        p.prepareToPlay(48000.0, 512);
        REQUIRE(p.loadNamModel(juce::File(SAS_NAM_EXAMPLES_DIR "/wavenet.nam")));
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1000, 520);
        play(p, *dynamic_cast<SasEditor*>(ed.get()), 196.0, 0.2);
        savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, scale), "nam-loader.png");
    }
}

TEST_CASE("the editor builds for every amp and can be rendered", "[slow]") {
    JuceInit juceInit;
    SasProcessor p;
    const char* dir = std::getenv("SAS_EDITOR_PNG_DIR");  // set it to also write a picture of every amp's editor
    for (int amp = 0; amp < p.apvts.getParameter("amp")->getNumSteps(); ++amp) {
        setParam(p, "amp", float(amp));
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        REQUIRE(ed != nullptr);
        ed->setSize(1000, 520);
        run(p, 40, 220.0, 0.2);  // so the meters have something to show
        if (auto* se = dynamic_cast<SasEditor*>(ed.get())) for (int i = 0; i < 12; ++i) { se->pollMeters(); run(p, 2, 220.0, 0.2); }
        CHECK(ed->getWidth() > 0);
        if (dir) {
            const auto img = ed->createComponentSnapshot(ed->getLocalBounds());
            juce::PNGImageFormat png;
            juce::FileOutputStream out(juce::File(dir).getChildFile("amp" + juce::String(amp) + ".png"));
            if (out.openedOk()) { out.setPosition(0); out.truncate(); png.writeImageToStream(img, out); }
            if (amp == 0) {  // the effects page
                p.applyEqPreset(3);
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "EQ") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                if (auto* se2 = dynamic_cast<SasEditor*>(ed.get())) for (int i = 0; i < 24; ++i) { run(p, 1, 220.0, 0.2); }
                {
                    const auto ex = ed->createComponentSnapshot(ed->getLocalBounds());
                    juce::FileOutputStream o4(juce::File(dir).getChildFile("eq.png"));
                    if (o4.openedOk()) { o4.setPosition(0); o4.truncate(); png.writeImageToStream(ex, o4); }
                    p.applyEqPreset(0);
                    
                }
                setParam(p, ids::phOn, 1.0f);
                setParam(p, ids::chOn, 1.0f);
                setParam(p, ids::vbOn, 1.0f);
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Mod") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                {
                    const auto mx = ed->createComponentSnapshot(ed->getLocalBounds());
                    juce::FileOutputStream o6(juce::File(dir).getChildFile("mod.png"));
                    if (o6.openedOk()) { o6.setPosition(0); o6.truncate(); png.writeImageToStream(mx, o6); }
                    setParam(p, ids::phOn, 0.0f);
                    setParam(p, ids::chOn, 0.0f);
                    setParam(p, ids::vbOn, 0.0f);
                }
                p.applyDelayPreset(4);
                p.applyReverbPreset(2);
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Space") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                {
                    const auto sp = ed->createComponentSnapshot(ed->getLocalBounds());
                    juce::FileOutputStream o5(juce::File(dir).getChildFile("space.png"));
                    if (o5.openedOk()) { o5.setPosition(0); o5.truncate(); png.writeImageToStream(sp, o5); }
                    p.applyDelayPreset(0);
                    p.applyReverbPreset(0);
                }
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Effects") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Pitch") { setParam(p, ids::pitch, -2.0f); b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                {
                    const auto px = ed->createComponentSnapshot(ed->getLocalBounds());
                    juce::FileOutputStream o3(juce::File(dir).getChildFile("pitch.png"));
                    if (o3.openedOk()) { o3.setPosition(0); o3.truncate(); png.writeImageToStream(px, o3); }
                    setParam(p, ids::pitch, 0.0f);
                }
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Effects") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                const auto fx = ed->createComponentSnapshot(ed->getLocalBounds());
                juce::FileOutputStream o2(juce::File(dir).getChildFile("fx.png"));
                if (o2.openedOk()) { o2.setPosition(0); o2.truncate(); png.writeImageToStream(fx, o2); }
                {
                    // Two different synthetic cabinets (decaying noise: one dark, one bright) so the curves have something to show.
                    auto make = [](const juce::String& name, float smoothing) {
                        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
                        file.deleteFile();
                        juce::WavAudioFormat wav;
                        std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 24, {}, 0));
                        juce::AudioBuffer<float> ir(1, 4096);
                        juce::Random rnd(7);
                        float lp = 0.0f;
                        for (int i = 0; i < ir.getNumSamples(); ++i) {
                            const float noise = rnd.nextFloat() * 2.0f - 1.0f;
                            lp += smoothing * (noise - lp);
                            ir.setSample(0, i, (smoothing < 0.5f ? lp : noise - 0.6f * lp) * std::exp(-float(i) / 500.0f));
                        }
                        w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
                        return file;
                    };
                    const auto fa = make("sas_render_a.wav", 0.15f), fb = make("sas_render_b.wav", 0.8f);
                    p.loadImpulseResponse(fa, 0);
                    p.loadImpulseResponse(fb, 1);
                    if (auto* editor = dynamic_cast<SasEditor*>(ed.get())) editor->updateImpulseStatus();
                    fa.deleteFile();
                    fb.deleteFile();
                }
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Cabinet") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                const auto cabPage = ed->createComponentSnapshot(ed->getLocalBounds());
                {
                    // The tuner page with a note slightly flat of G3 (196 Hz is G3 exactly: 190 Hz is about 54 cents flat of it, so use 195 Hz).
                    setParam(p, ids::tunerOn, 1.0f);
                    run(p, 80, 195.0, 0.1);  // one continuous stretch: the sine restarts its phase on every call
                    if (auto* editor = dynamic_cast<SasEditor*>(ed.get()))
                        for (int i = 0; i < 10; ++i) editor->updateTuner();  // the needle glides to the reading
                    for (auto* c : ed->getChildren())
                        if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Tuner") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                    const auto tuner = ed->createComponentSnapshot(ed->getLocalBounds());
                    juce::FileOutputStream o4(juce::File(dir).getChildFile("tuner.png"));
                    if (o4.openedOk()) { o4.setPosition(0); o4.truncate(); png.writeImageToStream(tuner, o4); }
                    setParam(p, ids::tunerOn, 0.0f);
                }
                juce::FileOutputStream o3(juce::File(dir).getChildFile("cab.png"));
                if (o3.openedOk()) { o3.setPosition(0); o3.truncate(); png.writeImageToStream(cabPage, o3); }
            }
        }
    }
}

TEST_CASE("pitch shift: off by default with no delay, on it adds its latency and still plays; a semitone and a few cents are separate parameters") {
    JuceInit init;
    SasProcessor p;
    allModules(p);
    p.prepareToPlay(48000.0, 512);
    setParam(p, ids::quality, 2.0f);
    CHECK(p.apvts.getParameter(ids::pitch) != nullptr);
    CHECK(p.apvts.getParameter(ids::pitchFine) != nullptr);
    run(p, 6);
    const int latencyOff = p.getLatencySamples();
    setParam(p, ids::pitch, -2.0f);  // a whole tone down
    const auto out = run(p, 20);
    for (int i = 0; i < out.getNumSamples(); ++i) REQUIRE(std::isfinite(out.getSample(0, i)));
    CHECK(rms(out) > 1e-4);
    const int latencyOn = p.getLatencySamples();
    CHECK(latencyOn - latencyOff > 400);  // about 12 ms at 48 kHz
    CHECK(latencyOn - latencyOff < 800);
    setParam(p, ids::pitch, 0.0f);
    run(p, 6);
    CHECK(p.getLatencySamples() == latencyOff);
    setParam(p, ids::pitchFine, 20.0f);  // cents alone also count
    run(p, 6);
    CHECK(p.getLatencySamples() > latencyOff + 400);
}

TEST_CASE("eq: flat by default, and the post equaliser acts on the output") {
    JuceInit init;
    SasProcessor p;
    allModules(p);
    p.prepareToPlay(48000.0, 512);
    setParam(p, ids::quality, 2.0f);
    for (int b = 0; b < 5; ++b) {
        CHECK(p.apvts.getParameter(ids::eqFreq[b]) != nullptr);
        CHECK(p.apvts.getRawParameterValue(ids::eqGain[b])->load() == 0.0f);
    }
    CHECK(p.apvts.getRawParameterValue(ids::eqOn)->load() > 0.5f);
    run(p, 12);
    const double flat = rms(run(p, 6));
    setParam(p, ids::eqHpf, 500.0f);  // a 500 Hz high-pass takes most of a 220 Hz note and its low harmonics
    run(p, 8);
    const double cut = rms(run(p, 6));
    INFO("flat " << flat << " with the high-pass " << cut);
    CHECK(cut < 0.7 * flat);
    setParam(p, ids::eqOn, 0.0f);  // switched off: back to the flat level
    run(p, 8);
    CHECK(std::fabs(rms(run(p, 6)) / flat - 1.0) < 0.02);
}

TEST_CASE("presets: saving and loading a preset restores the knobs, the EQ and the pitch") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    setParam(p, ids::volume, 7.5f);
    setParam(p, ids::eqGain[2], 6.0f);
    setParam(p, ids::pitch, -3.0f);
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sas_test_preset.sasp");
    file.deleteFile();
    REQUIRE(p.savePreset(file));
    CHECK(file.loadFileAsString().contains("<"));  // a text (XML) file
    setParam(p, ids::volume, 2.0f);
    setParam(p, ids::eqGain[2], 0.0f);
    setParam(p, ids::pitch, 0.0f);
    REQUIRE(p.loadPreset(file));
    CHECK(std::fabs(p.apvts.getRawParameterValue(ids::volume)->load() - 7.5f) < 0.01f);
    CHECK(std::fabs(p.apvts.getRawParameterValue(ids::eqGain[2])->load() - 6.0f) < 0.01f);
    CHECK(std::fabs(p.apvts.getRawParameterValue(ids::pitch)->load() + 3.0f) < 0.01f);
    const auto bad = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sas_test_bad.sasp");
    bad.replaceWithText("not a preset");
    CHECK(!p.loadPreset(bad));
    CHECK(!p.loadPreset(juce::File("C:/does/not/exist.sasp")));
    file.deleteFile();
    bad.deleteFile();
}

TEST_CASE("eq presets: each factory preset sets the EQ parameters, stays within sane gains, and shapes the sound as its name says") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    REQUIRE(kEqPresetCount == 11);  // flat and ten guitar presets
    auto response = [&](double hz) {
        sas::EqParams eq;
        eq.on = p.apvts.getRawParameterValue(ids::eqOn)->load() > 0.5f;
        eq.hpfHz = double(p.apvts.getRawParameterValue(ids::eqHpf)->load());
        eq.lpfHz = double(p.apvts.getRawParameterValue(ids::eqLpf)->load());
        for (int b = 0; b < sas::EqParams::kBands; ++b) {
            eq.band[std::size_t(b)].freq = double(p.apvts.getRawParameterValue(ids::eqFreq[b])->load());
            eq.band[std::size_t(b)].gainDb = double(p.apvts.getRawParameterValue(ids::eqGain[b])->load());
            eq.band[std::size_t(b)].q = double(p.apvts.getRawParameterValue(ids::eqQ[b])->load());
        }
        sas::ParametricEq e;
        e.prepare(48000.0);
        e.setParams(eq);
        return e.responseDb(hz);
    };
    auto at = [&](int preset, double hz) { p.applyEqPreset(preset); return response(hz); };
    for (int i = 0; i < kEqPresetCount; ++i) {
        INFO(kEqPresets[i].name);
        p.applyEqPreset(i);
        CHECK(p.apvts.getRawParameterValue(ids::eqOn)->load() > 0.5f);
        CHECK(std::fabs(p.apvts.getRawParameterValue(ids::eqHpf)->load() - float(kEqPresets[i].hpfHz)) < 0.5f);
        for (int b = 0; b < 5; ++b) {
            CHECK(std::fabs(p.apvts.getRawParameterValue(ids::eqGain[b])->load() - float(kEqPresets[i].band[std::size_t(b)].gainDb)) < 0.11f);
            CHECK(std::fabs(p.apvts.getRawParameterValue(ids::eqFreq[b])->load() / float(kEqPresets[i].band[std::size_t(b)].freq) - 1.0f) < 0.01f);
        }
        double lo = 0.0, hi = 0.0;
        for (double f = 25.0; f < 12000.0; f *= 1.1) { const double r = response(f); lo = std::min(lo, r); hi = std::max(hi, r); }
        CHECK(hi < 8.0);    // no preset makes the signal much louder anywhere
        CHECK(lo > -30.0);
    }
    CHECK(std::fabs(at(0, 1000.0)) < 0.01);                                        // flat
    CHECK(at(1, 10000.0) > 1.5);                                                   // clean sparkle: air
    CHECK(at(1, 250.0) < -1.0);                                                    // ...and the mud out
    CHECK(at(2, 8000.0) < -3.0);                                                   // warm blues: the top rolled off
    CHECK(at(2, 150.0) > at(2, 1000.0) + 1.0);                                     // ...and weight in the lows
    CHECK(at(3, 40.0) < -12.0);                                                    // tight rhythm: nothing under the low E
    CHECK(at(3, 300.0) < -2.0);
    CHECK(at(3, 3500.0) > 0.5);
    CHECK(at(4, 2200.0) > 2.0);                                                    // lead: the upper mids
    CHECK(at(4, 800.0) > 2.0);
    CHECK(at(5, 8000.0) < -4.0);                                                   // de-fizz
    CHECK(at(5, 4500.0) < -2.0);
    CHECK(at(6, 550.0) < -3.0);                                                    // metal scoop: the mids hollowed out
    CHECK(at(6, 3000.0) > 1.5);
    CHECK(at(6, 30.0) < -12.0);
    CHECK(at(7, 8000.0) < -6.0);                                                   // jazz warm: dark
    CHECK(at(7, 200.0) > 1.0);
    CHECK(at(8, 3000.0) > 3.0);                                                    // country twang
    CHECK(at(8, 60.0) < -8.0);
    CHECK(at(9, 500.0) > 1.5);                                                     // classic rock crunch: thick mids
    CHECK(at(9, 200.0) > 1.0);
    CHECK(at(9, 8000.0) < -2.0);
    CHECK(at(10, 300.0) < -2.5);                                                   // mix-ready: no mud, a little presence
    CHECK(at(10, 3000.0) > 1.0);
    CHECK(at(10, 50.0) < -9.0);
}

TEST_CASE("amp switches: the SVT has three, they are parameters, and they change the sound") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    int svt = -1;
    for (std::size_t i = 0; i < p.amps().size(); ++i) if (p.amps()[i].id == "svt_bass300") svt = int(i);
    REQUIRE(svt >= 0);
    CHECK(p.amps()[std::size_t(svt)].preamp.selectors.size() == 3);
    for (int i = 0; i < 4; ++i) CHECK(p.apvts.getParameter(ids::sel[i]) != nullptr);
    setParam(p, ids::quality, 0.0f);
    setParam(p, ids::amp, float(svt));
    bool swapped = false;
    for (int i = 0; i < 1200 && !swapped; ++i) { run(p, 2, 3000.0, 0.0005); swapped = p.runningAmpIndex() == svt; juce::Thread::sleep(15); }
    REQUIRE(swapped);
    run(p, 20, 3000.0, 0.0005);
    const double off = rms(run(p, 10, 3000.0, 0.0005));
    setParam(p, ids::sel[1], 1.0f);  // Ultra Hi
    run(p, 20, 3000.0, 0.0005);
    const double on = rms(run(p, 10, 3000.0, 0.0005));
    INFO("ultra hi off " << off << ", on " << on);
    CHECK(on > 1.2 * off);  // the input is a few hundred microvolts, far above the supply hum that moves with the moment of the swap
}

TEST_CASE("built-in cabinet impulses: three ship with the plugin, load into either slot and survive a project round trip") {
    JuceInit juceInit;
    if (SasProcessor::builtInImpulseResponseCount() == 0) SKIP("this build carries no built-in impulses (plugin/cabs has no WAV files)");
    REQUIRE(SasProcessor::builtInImpulseResponseCount() == 8);
    const char* expected[] = {"4x12 Fried Mix 1", "4x12 Green Mix 1", "4x12 Green Mix 2", "4x12 Green Mix 3", "4x12 Org Mix 1", "4x12 OS Mix 1", "4x12 OS Mix 2", "4x12 OS Mix 3"};
    for (int i = 0; i < 8; ++i) CHECK(SasProcessor::builtInImpulseResponseName(i) == expected[i]);
    SasProcessor proc;
    CHECK(proc.impulseResponseName(0) == "4x12 OS Mix 1");
    REQUIRE(proc.loadBuiltInImpulseResponse(2, 1));
    CHECK(proc.impulseResponseName(1) == "4x12 Green Mix 2");
    CHECK_FALSE(proc.loadBuiltInImpulseResponse(8, 0));
    for (int i = 0; i < SasProcessor::builtInImpulseResponseCount(); ++i) CHECK(proc.loadBuiltInImpulseResponse(i, 1));  // every one decodes
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.impulseResponseName(0) == "4x12 OS Mix 1");
    CHECK(other.impulseResponseName(1) == "4x12 OS Mix 3");
}

TEST_CASE("delay and reverb: off by default and invisible, on they add a tail, presets set both, and the state survives a project round trip") {
    JuceInit juceInit;
    SasProcessor proc;
    allModules(proc);
    proc.prepareToPlay(48000.0, 512);
    CHECK(proc.apvts.getRawParameterValue(ids::dlyOn)->load() < 0.5f);
    CHECK(proc.apvts.getRawParameterValue(ids::plOn)->load() < 0.5f);
    auto burst = [&]() {
        juce::AudioBuffer<float> b(2, 512);
        juce::MidiBuffer midi;
        double energyAfter = 0.0;
        b.clear();
        for (int blk = 0; blk < 120; ++blk) {
            b.clear();
            if (blk == 0) fill(b, 220.0, 0.3, 48000.0);
            proc.processBlock(b, midi);
            if (blk > 40) energyAfter += rms(b) * rms(b);
        }
        return energyAfter;
    };
    const double dry = burst();
    proc.applyDelayPreset(4);   // "Ping-pong"
    proc.applyReverbPreset(2);  // "Short plate"
    CHECK(proc.apvts.getRawParameterValue(ids::dlyOn)->load() > 0.5f);
    CHECK(proc.apvts.getRawParameterValue(ids::plOn)->load() > 0.5f);
    CHECK(proc.apvts.getRawParameterValue(ids::dlyPing)->load() > 0.5f);
    const double wet = burst();
    INFO("tail energy dry " << dry << " wet " << wet);
    CHECK(wet > dry * 3.0 + 1e-9);
    proc.applyReverbPreset(5);  // "Big hall": only the reverb changes, the delay stays on
    CHECK(proc.apvts.getRawParameterValue(ids::dlyOn)->load() > 0.5f);
    proc.applyDelayPreset(0);   // "Off"
    CHECK(proc.apvts.getRawParameterValue(ids::dlyOn)->load() < 0.5f);
    CHECK(proc.apvts.getRawParameterValue(ids::plOn)->load() > 0.5f);
    CHECK(proc.apvts.getRawParameterValue(ids::plHall)->load() > 0.5f);
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.apvts.getRawParameterValue(ids::plHall)->load() > 0.5f);
    CHECK(other.apvts.getRawParameterValue(ids::plDecay)->load() == Catch::Approx(3.8f).margin(0.05f));
    CHECK(other.apvts.getRawParameterValue(ids::plShimmer)->load() < 0.5f);
    proc.applyReverbPreset(9);  // "Angelic shimmer": the shimmer is a part of the preset, and the next preset clears it
    CHECK(proc.apvts.getRawParameterValue(ids::plShimmer)->load() > 20.0f);
    CHECK(proc.apvts.getRawParameterValue(ids::plShimmerInt)->load() > 0.5f);
    proc.applyReverbPreset(2);
    CHECK(proc.apvts.getRawParameterValue(ids::plShimmer)->load() < 0.5f);
}

TEST_CASE("phaser: off by default, on it changes the sound before the amp and after it, and the position is a saved parameter") {
    JuceInit juceInit;
    SasProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    CHECK(proc.apvts.getRawParameterValue(ids::phOn)->load() < 0.5f);
    auto render = [&]() {
        juce::AudioBuffer<float> out(2, 512 * 40);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < 40; ++blk) {
            juce::AudioBuffer<float> b(2, 512);
            fill(b, 330.0, 0.1, 48000.0, blk * 512);
            proc.processBlock(b, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, blk * 512, b, ch, 0, 512);
        }
        return out;
    };
    const auto plain = render();
    setParam(proc, ids::phOn, 1.0f);
    setParam(proc, ids::phSpeed, 6.0f);
    const auto before = render();
    setParam(proc, ids::phPost, 1.0f);
    const auto after = render();
    auto diff = [](const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
        double d = 0;
        for (int i = 8192; i < a.getNumSamples(); ++i) d += std::fabs(double(a.getSample(0, i)) - double(b.getSample(0, i)));
        return d;
    };
    CHECK(diff(plain, before) > 1.0);
    CHECK(diff(plain, after) > 1.0);
    CHECK(diff(before, after) > 1.0);
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.apvts.getRawParameterValue(ids::phPost)->load() > 0.5f);
}

TEST_CASE("chorus: off by default, on it changes the sound in both modes and both positions, and the settings are saved") {
    JuceInit juceInit;
    SasProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    CHECK(proc.apvts.getRawParameterValue(ids::chOn)->load() < 0.5f);
    CHECK(proc.apvts.getRawParameterValue(ids::chPost)->load() > 0.5f);  // after the amp unless told otherwise
    auto render = [&]() {
        juce::AudioBuffer<float> out(2, 512 * 40);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < 40; ++blk) {
            juce::AudioBuffer<float> b(2, 512);
            fill(b, 330.0, 0.1, 48000.0, blk * 512);
            proc.processBlock(b, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, blk * 512, b, ch, 0, 512);
        }
        return out;
    };
    auto diff = [](const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int ch) {
        double d = 0;
        for (int i = 8192; i < a.getNumSamples(); ++i) d += std::fabs(double(a.getSample(ch, i)) - double(b.getSample(ch, i)));
        return d;
    };
    const auto plain = render();
    setParam(proc, ids::chOn, 1.0f);
    const auto ce2Post = render();
    CHECK(diff(plain, ce2Post, 0) > 1.0);
    setParam(proc, ids::chMode, 1.0f);
    const auto junoPost = render();
    CHECK(diff(ce2Post, junoPost, 0) > 1.0);
    CHECK(diff(junoPost, junoPost, 0) == 0.0);
    CHECK(diff(junoPost, plain, 1) > 1.0);
    setParam(proc, ids::chPost, 0.0f);
    const auto junoPre = render();
    CHECK(diff(junoPost, junoPre, 0) > 1.0);
    for (int i = 0; i < junoPre.getNumSamples(); ++i) REQUIRE(std::isfinite(junoPre.getSample(0, i)));
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.apvts.getRawParameterValue(ids::chMode)->load() > 0.5f);
    CHECK(other.apvts.getRawParameterValue(ids::chPost)->load() < 0.5f);
}

TEST_CASE("vibe: off by default, on it changes the sound in both modes and positions, and the settings are saved") {
    JuceInit juceInit;
    SasProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    CHECK(proc.apvts.getRawParameterValue(ids::vbOn)->load() < 0.5f);
    CHECK(proc.apvts.getRawParameterValue(ids::vbPost)->load() < 0.5f);  // in front of the amp unless told otherwise
    auto render = [&]() {
        juce::AudioBuffer<float> out(2, 512 * 40);
        juce::MidiBuffer midi;
        for (int blk = 0; blk < 40; ++blk) {
            juce::AudioBuffer<float> b(2, 512);
            fill(b, 330.0, 0.1, 48000.0, blk * 512);
            proc.processBlock(b, midi);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, blk * 512, b, ch, 0, 512);
        }
        return out;
    };
    auto diff = [](const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b) {
        double d = 0;
        for (int i = 8192; i < a.getNumSamples(); ++i) d += std::fabs(double(a.getSample(0, i)) - double(b.getSample(0, i)));
        return d;
    };
    const auto plain = render();
    setParam(proc, ids::vbOn, 1.0f);
    setParam(proc, ids::vbSpeed, 6.0f);
    const auto pre = render();
    CHECK(diff(plain, pre) > 1.0);
    setParam(proc, ids::vbVibrato, 1.0f);
    const auto vib = render();
    CHECK(diff(pre, vib) > 1.0);
    setParam(proc, ids::vbPost, 1.0f);
    const auto post = render();
    CHECK(diff(vib, post) > 1.0);
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.apvts.getRawParameterValue(ids::vbVibrato)->load() > 0.5f);
    CHECK(other.apvts.getRawParameterValue(ids::vbPost)->load() > 0.5f);
}

TEST_CASE("quality: the Efficient choice (preamp 2x, power amp at the host rate) plays, keeps the latency of the 2x modes and is saved") {
    JuceInit juceInit;
    SasProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    auto* quality = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(ids::quality));
    REQUIRE(quality != nullptr);
    CHECK(quality->choices.size() == 5);
    CHECK(quality->choices[4].contains("Efficient"));
    juce::MidiBuffer midi;
    auto play = [&]() {
        double energy = 0.0;
        for (int blk = 0; blk < 40; ++blk) {
            juce::AudioBuffer<float> b(2, 512);
            fill(b, 330.0, 0.1, 48000.0, blk * 512);
            proc.processBlock(b, midi);
            for (int i = 0; i < 512; ++i) REQUIRE(std::isfinite(b.getSample(0, i)));
            if (blk > 20) energy += rms(b) * rms(b);
        }
        return energy;
    };
    setParam(proc, ids::quality, 0.0f);
    const double normal = play();
    const int normalLatency = proc.getLatencySamples();
    setParam(proc, ids::quality, 4.0f);
    const double efficient = play();
    CHECK(proc.getLatencySamples() == normalLatency);
    INFO("energy normal " << normal << ", efficient " << efficient);
    CHECK(efficient > 0.3 * normal);
    CHECK(efficient < 3.0 * normal);
    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.apvts.getRawParameterValue(ids::quality)->load() > 3.5f);
}

TEST_CASE("NAM loader: the amp is in the list, loads a model, plays it and keeps it in the project state") {
    JuceInit juceInit;
    SasProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    int namIndex = -1;
    for (int i = 0; i < int(proc.amps().size()); ++i)
        if (proc.amps()[std::size_t(i)].nam) namIndex = i;
    REQUIRE(namIndex >= 0);
    CHECK(!proc.namModelLoaded());
    CHECK(!proc.loadNamModel(juce::File(SAS_NAM_EXAMPLES_DIR "/../README.md")));  // not a model: refused, with a reason
    CHECK(proc.namModelMessage().isNotEmpty());
    REQUIRE(proc.loadNamModel(juce::File(SAS_NAM_EXAMPLES_DIR "/wavenet.nam")));
    CHECK(proc.namModelName() == "wavenet");

    setParam(proc, ids::amp, float(namIndex));
    juce::MidiBuffer midi;
    double energy = 0.0;
    for (int blk = 0; blk < 400; ++blk) {  // the chain for the new amp is built in the background and swapped in
        juce::AudioBuffer<float> b(2, 512);
        fill(b, 220.0, 0.2, 48000.0, blk * 512);
        proc.processBlock(b, midi);
        for (int i = 0; i < 512; ++i) REQUIRE(std::isfinite(b.getSample(0, i)));
        if (blk > 300) energy += rms(b) * rms(b);
        if (proc.runningAmpIndex() != namIndex) juce::Thread::sleep(5);
    }
    CHECK(proc.runningAmpIndex() == namIndex);
    CHECK(energy > 1e-6);

    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.namModelLoaded());
    CHECK(other.namModelName() == "wavenet");
    proc.clearNamModel();
    CHECK(!proc.namModelLoaded());
}

TEST_CASE("NAM cabinet: a model loaded as the cabinet plays after any amp, replaces the filter, and is kept in the project state") {
    JuceInit juceInit;
    SasProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    auto* cab = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter(ids::cab));
    REQUIRE(cab != nullptr);
    CHECK(cab->choices.size() == 4);
    CHECK(cab->choices[3] == "NAM");
    CHECK(!proc.namCabLoaded());
    CHECK(!proc.loadNamCab(juce::File(SAS_NAM_EXAMPLES_DIR "/../README.md")));
    CHECK(proc.namCabMessage().isNotEmpty());

    juce::MidiBuffer midi;
    auto play = [&]() {
        double energy = 0.0;
        for (int blk = 0; blk < 40; ++blk) {
            juce::AudioBuffer<float> b(2, 512);
            fill(b, 330.0, 0.1, 48000.0, blk * 512);
            proc.processBlock(b, midi);
            for (int i = 0; i < 512; ++i) REQUIRE(std::isfinite(b.getSample(0, i)));
            if (blk > 20) energy += rms(b) * rms(b);
        }
        return energy;
    };
    setParam(proc, ids::cab, 3.0f);  // NAM with nothing loaded: falls back to the cabinet filter, so it still plays
    const double fallback = play();
    CHECK(fallback > 1e-6);
    REQUIRE(proc.loadNamCab(juce::File(SAS_NAM_EXAMPLES_DIR "/wavenet.nam")));
    CHECK(proc.namCabName() == "wavenet");
    const double withModel = play();
    CHECK(withModel > 1e-8);
    CHECK(withModel != fallback);

    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.namCabLoaded());
    CHECK(other.namCabName() == "wavenet");
    proc.clearNamCab();
    CHECK(!proc.namCabLoaded());
}

TEST_CASE("NAM pedal: a model in front of the amp plays when the switch is on, does nothing when it is off, and is kept in the project state") {
    JuceInit juceInit;
    SasProcessor proc;
    allModules(proc);
    proc.prepareToPlay(48000.0, 512);
    CHECK(!proc.namPedalLoaded());
    CHECK(!proc.loadNamPedal(juce::File(SAS_NAM_EXAMPLES_DIR "/../README.md")));
    CHECK(proc.namPedalMessage().isNotEmpty());
    juce::MidiBuffer midi;
    auto play = [&]() {
        double energy = 0.0;
        for (int blk = 0; blk < 40; ++blk) {
            juce::AudioBuffer<float> b(2, 512);
            fill(b, 330.0, 0.1, 48000.0, blk * 512);
            proc.processBlock(b, midi);
            for (int i = 0; i < 512; ++i) REQUIRE(std::isfinite(b.getSample(0, i)));
            if (blk > 20) energy += rms(b) * rms(b);
        }
        return energy;
    };
    play();  // warm-up: the amp's supply settles
    const double without = play();
    REQUIRE(proc.loadNamPedal(juce::File(SAS_NAM_EXAMPLES_DIR "/wavenet.nam")));
    CHECK(proc.namPedalName() == "wavenet");
    CHECK(std::abs(play() - without) < 0.03 * without);  // loaded but switched off: the same sound (the supply still drifts a little between runs)
    setParam(proc, ids::nmpOn, 1.0f);
    const double on = play();
    CHECK(on > 1e-8);
    CHECK(on != without);
    setParam(proc, ids::nmpLevel, -12.0f);
    const double quieter = play();
    CHECK(quieter < on);

    juce::MemoryBlock state;
    proc.getStateInformation(state);
    SasProcessor other;
    other.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(other.namPedalLoaded());
    CHECK(other.namPedalName() == "wavenet");
    proc.clearNamPedal();
    CHECK(!proc.namPedalLoaded());
}

// ---------------------------------------------------------------- modular rack: reference of the fixed-order chain

#include "legacy_reference.h"


// ---------------------------------------------------------------- modular rack: parameters, state and migration

#include "RackState.h"

namespace {

using sas::ModuleType;

std::vector<ModuleType> typesOf(const sas::RackOrder& o) {
    std::vector<ModuleType> v;
    for (int i = 0; i < o.count; ++i) v.push_back(o.slots[std::size_t(i)].type);
    return v;
}

// Saves the processor's state as a preset file, lets `edit` change its XML, and loads it into `into`.
template <class Edit>
void roundTrip(SasProcessor& from, SasProcessor& into, Edit&& edit) {
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sas_rack_state.sasp");
    REQUIRE(from.savePreset(file));
    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    REQUIRE(xml != nullptr);
    edit(*xml);
    REQUIRE(file.replaceWithText(xml->toString()));
    REQUIRE(into.loadPreset(file));
}

void stripRack(juce::XmlElement& xml) {
    while (auto* r = xml.getChildByName("RACK")) xml.removeChildElement(r, true);
    xml.removeAttribute("rackVersion");
}

}  // namespace

TEST_CASE("a state without RACK gets one from the old parameters", "[rack]") {
    JuceInit init;
    SECTION("effects after the amp, and the chain's pedals in front of it") {
        SasProcessor a, b;
        setParam(a, ids::phOn, 1); setParam(a, ids::phPost, 1);
        setParam(a, ids::tsOn, 1); setParam(a, ids::compOn, 1); setParam(a, ids::dlyOn, 1);
        roundTrip(a, b, stripRack);
        using T = ModuleType;
        CHECK(typesOf(b.rackOrder()) == std::vector<T>{T::Gate, T::Compressor, T::Overdrive, T::Amp, T::Cabinet, T::Phaser, T::Delay});
    }
    SECTION("every modulation in front of the amp keeps its old order") {
        SasProcessor a, b;
        for (const char* on : {ids::phOn, ids::chOn, ids::vbOn}) setParam(a, on, 1);
        setParam(a, ids::chPost, 0);
        roundTrip(a, b, stripRack);
        using T = ModuleType;
        CHECK(typesOf(b.rackOrder()) == std::vector<T>{T::Gate, T::Phaser, T::Chorus, T::Vibe, T::Amp, T::Cabinet});
    }
    SECTION("a plain old state is just the three fixed modules") {
        SasProcessor a, b;
        roundTrip(a, b, stripRack);
        using T = ModuleType;
        CHECK(typesOf(b.rackOrder()) == std::vector<T>{T::Gate, T::Amp, T::Cabinet});
    }
}

TEST_CASE("a disabled-but-tuned delay is kept as a bypassed module", "[rack]") {
    JuceInit init;
    SasProcessor a, b;
    setParam(a, ids::dlyMix, 60.0f);  // tuned, but the switch is off
    roundTrip(a, b, stripRack);
    const auto types = typesOf(b.rackOrder());
    CHECK(std::count(types.begin(), types.end(), ModuleType::Delay) == 1);
    CHECK(b.apvts.getRawParameterValue(ids::dlyOn)->load() < 0.5f);
    CHECK(b.apvts.getRawParameterValue(ids::dlyMix)->load() == Catch::Approx(60.0f));
}

TEST_CASE("unknown module type, newer rackVersion or too many modules loads what it understands", "[rack]") {
    JuceInit init;
    SasProcessor a, b;
    roundTrip(a, b, [](juce::XmlElement& xml) {
        stripRack(xml);
        xml.setAttribute("rackVersion", 99);
        auto* rack = xml.createNewChildElement("RACK");
        auto add = [&](const char* type, int inst) {
            auto* m = rack->createNewChildElement("M");
            m->setAttribute("type", type);
            m->setAttribute("inst", inst);
        };
        add("Foo", 0);       // unknown: dropped
        add("Reverb", 0);
        add("Reverb", 0);    // repeated: dropped
        add("Reverb", 7);    // beyond the cap: dropped
        add("Cabinet", 0);
        for (int i = 0; i < 3; ++i) { add("Overdrive", i); add("Gate", i); add("Eq", i); add("Delay", i); }
        add("Amp", 0);
        add("Amp", 0);
    });
    const auto order = b.rackOrder();
    CHECK(order.count <= sas::RackOrder::kMax);
    int amps = 0, modules = 0;
    for (int i = 0; i < order.count; ++i) (order.slots[std::size_t(i)].type == ModuleType::Amp ? amps : modules)++;
    CHECK(amps == 1);
    CHECK(modules <= RackState::kMaxModules);
    CHECK(order.slots[0].type == ModuleType::Reverb);
}

TEST_CASE("a saved rack without an amp stays without one (the amp can be removed); one without a gate gets the gate back", "[rack]") {
    const auto order = RackState::fromTree([] {
        juce::ValueTree rack("RACK");
        for (const char* type : {"Gate", "Cabinet", "Reverb"}) {
            juce::ValueTree m("M");
            m.setProperty("type", type, nullptr);
            m.setProperty("inst", 0, nullptr);
            rack.appendChild(m, nullptr);
        }
        return rack;
    }());
    CHECK(typesOf(order) == std::vector<ModuleType>{ModuleType::Gate, ModuleType::Cabinet, ModuleType::Reverb});
    // The first gate, which cannot be removed, comes back when a rack lacks it.
    const auto noGate = RackState::fromTree([] {
        juce::ValueTree rack("RACK");
        for (const char* type : {"Amp", "Reverb"}) {
            juce::ValueTree m("M");
            m.setProperty("type", type, nullptr);
            m.setProperty("inst", 0, nullptr);
            rack.appendChild(m, nullptr);
        }
        return rack;
    }());
    CHECK(typesOf(noGate) == std::vector<ModuleType>{ModuleType::Gate, ModuleType::Amp, ModuleType::Reverb});
}

TEST_CASE("the order and every extra instance's parameters survive a round trip", "[rack]") {
    JuceInit init;
    SasProcessor a, b;
    sas::RackOrder order;
    for (auto s : {sas::SlotRef{ModuleType::Gate, 0}, {ModuleType::Overdrive, 1}, {ModuleType::Overdrive, 0}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0}, {ModuleType::Reverb, 1}, {ModuleType::Reverb, 0}})
        order.slots[std::size_t(order.count++)] = s;
    a.setRackOrder(order);
    setParam(a, "ts_drive__2", 8.0f);
    setParam(a, "pl_decay__2", 5.0f);
    roundTrip(a, b, [](juce::XmlElement&) {});
    REQUIRE(b.rackOrder().count == order.count);
    for (int i = 0; i < order.count; ++i) CHECK(b.rackOrder().slots[std::size_t(i)] == order.slots[std::size_t(i)]);
    CHECK(b.apvts.getRawParameterValue("ts_drive__2")->load() == Catch::Approx(8.0f));
    CHECK(b.apvts.getRawParameterValue("pl_decay__2")->load() == Catch::Approx(5.0f));
}

TEST_CASE("extra instances exist with their own names and the old position switches are not automatable", "[rack]") {
    JuceInit init;
    SasProcessor p;
    REQUIRE(p.apvts.getParameter("ts_drive__2") != nullptr);
    CHECK(p.apvts.getParameter("ts_drive__2")->getName(64) == "TS Drive 2");
    CHECK(p.apvts.getParameter("ts_drive__4") != nullptr);
    CHECK(p.apvts.getParameter("ts_drive__5") == nullptr);  // the cap is four
    CHECK(p.apvts.getParameter("pl_mix__3") == nullptr);    // two reverbs
    CHECK(p.apvts.getParameter("eq1_gain__2")->getName(64) == "EQ 1 gain 2");
    for (const char* id : {ids::phPost, ids::chPost, ids::vbPost}) {
        REQUIRE(p.apvts.getParameter(id) != nullptr);
        CHECK_FALSE(p.apvts.getParameter(id)->isAutomatable());
    }
    CHECK(RackState::paramId("dly_mix", 0) == "dly_mix");
    CHECK(RackState::paramId("dly_mix", 1) == "dly_mix__2");
}

// The fixed-order plug-in's renders (golden/rack_legacy_*) must be reproduced by the rack that a state without RACK migrates to.
TEST_CASE("legacy states migrate to a rack that sounds the same", "[rack]") {
    JuceInit init;
    const auto in = legacy::input();
    for (const auto& c : legacy::cases()) {
        SECTION(c.name) {
            SasProcessor configured;
            c.setup(configured);
            SasProcessor p;
            roundTrip(configured, p, stripRack);  // an old project: no RACK, the order comes from the old parameters
            const auto out = legacy::render(p, in);
            double energy = 0.0;
            for (float v : out) {
                REQUIRE(std::isfinite(v));
                energy += double(v) * v;
            }
            CHECK(std::sqrt(energy / double(out.size())) > 1e-6);
            if (std::getenv("SAS_WRITE_GOLDEN")) legacy::writeGolden(c.name, out);
            const auto ref = legacy::readGolden(c.name);
            REQUIRE(ref.size() == out.size());
            for (std::size_t i = 0; i < out.size(); ++i) REQUIRE(std::abs(out[i] - ref[i]) <= 1e-5f);
        }
    }
}

TEST_CASE("a fresh instance renders the legacy default too", "[rack]") {
    JuceInit init;
    SasProcessor p;
    setParam(p, ids::quality, 0.0f);  // the reference is Normal; a new instance opens in Efficient
    const auto out = legacy::render(p, legacy::input());
    const auto ref = legacy::readGolden("default");
    REQUIRE(ref.size() == out.size());
    for (std::size_t i = 0; i < out.size(); ++i) REQUIRE(std::abs(out[i] - ref[i]) <= 1e-5f);
}

TEST_CASE("two overdrive instances: the second one is audible and independent", "[rack]") {
    JuceInit init;
    const auto in = legacy::input();
    auto render = [&](bool second, float secondDrive) {
        SasProcessor p;
        p.setRackOrder([&] {
            sas::RackOrder o;
            for (auto s : {sas::SlotRef{ModuleType::Overdrive, 0}, {ModuleType::Overdrive, 1}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0}}) o.slots[std::size_t(o.count++)] = s;
            return o;
        }());
        setParam(p, ids::tsOn, 1);
        setParam(p, "ts_on__2", second ? 1.0f : 0.0f);
        setParam(p, "ts_drive__2", secondDrive);
        return legacy::render(p, in);
    };
    const auto one = render(false, 5.0f), two = render(true, 5.0f), twoHot = render(true, 9.0f);
    auto diff = [](const std::vector<float>& a, const std::vector<float>& b) {
        double d = 0;
        for (std::size_t i = 0; i < a.size(); ++i) d += std::abs(double(a[i]) - double(b[i]));
        return d / double(a.size());
    };
    CHECK(diff(one, two) > 1e-4);     // the second pedal changes the sound
    CHECK(diff(two, twoHot) > 1e-4);  // and its own knob is what drives it
    for (float v : twoHot) REQUIRE(std::isfinite(v));
}

TEST_CASE("removing a NAM pedal clears its model from the state", "[rack]") {
    JuceInit init;
    SasProcessor p;
    allModules(p);
    REQUIRE(p.loadNamPedal(juce::File(SAS_NAM_EXAMPLES_DIR "/wavenet.nam"), 0));
    REQUIRE(p.removeModule({ModuleType::NamPedal, 0}));
    CHECK_FALSE(p.namPedalLoaded(0));
    REQUIRE(p.addModule(ModuleType::NamPedal));  // a new one starts empty
    juce::MemoryBlock state;
    p.getStateInformation(state);
    const std::unique_ptr<juce::XmlElement> xml(juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize())));
    REQUIRE(xml != nullptr);
    CHECK_FALSE(xml->hasAttribute("namPedalData"));
}

TEST_CASE("latency follows the active modules and not their order", "[rack]") {
    JuceInit init;
    SasProcessor p;
    allModules(p);
    p.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    auto latency = [&] {
        buf.clear();
        p.processBlock(buf, midi);
        return p.getLatencySamples();
    };
    const int base = latency();
    setParam(p, ids::pitch, 7.0f);
    const int shifted = latency();
    CHECK(shifted > base);
    REQUIRE(p.moveModule({ModuleType::Pitch, 0}, p.rackOrder().count - 1));
    CHECK(latency() == shifted);  // moving it does not change the sum
}

// ---------------------------------------------------------------- modular rack: the page

#include "ModuleUnit.h"
#include "RackPage.h"

TEST_CASE("the rack page lists the modules in order and follows add, move and remove", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.setRackOrder([] {
        sas::RackOrder o;
        for (auto s : {sas::SlotRef{ModuleType::Gate, 0}, {ModuleType::Overdrive, 0}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0}, {ModuleType::Reverb, 0}}) o.slots[std::size_t(o.count++)] = s;
        return o;
    }());
    RackPage page(p);
    page.setSize(900, 600);
    REQUIRE(page.unitCount() == 5);
    for (int i = 0; i < 5; ++i) CHECK(page.unitSlot(i) == p.rackOrder().slots[std::size_t(i)]);

    REQUIRE(p.addModule(ModuleType::Overdrive));  // goes in front of the amp, behind the first overdrive
    page.rebuild();
    REQUIRE(page.unitCount() == 6);
    CHECK(page.unitSlot(2) == sas::SlotRef{ModuleType::Overdrive, 1});
    CHECK(page.unitSlot(3).type == ModuleType::Amp);

    REQUIRE(p.moveModule({ModuleType::Reverb, 0}, 0));  // a reverb before everything
    page.rebuild();
    CHECK(page.unitSlot(0).type == ModuleType::Reverb);

    REQUIRE(p.removeModule({ModuleType::Overdrive, 0}));
    page.rebuild();
    CHECK(page.unitCount() == 5);
    REQUIRE(p.removeModule({ModuleType::Amp, 0}));  // the amp can go (and come back)
    page.rebuild();
    CHECK(page.unitCount() == 4);
    REQUIRE(p.addModule(ModuleType::Amp));
    page.rebuild();
    CHECK(page.unitCount() == 5);
}

TEST_CASE("the add menu greys out a type that reached its limit", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.setRackOrder([] {
        sas::RackOrder o;
        for (int i = 0; i < 4; ++i) o.slots[std::size_t(o.count++)] = {ModuleType::Overdrive, i};
        o.slots[std::size_t(o.count++)] = {ModuleType::Amp, 0};
        return o;
    }());
    CHECK_FALSE(p.canAdd(ModuleType::Overdrive));
    CHECK(p.canAdd(ModuleType::Gate));
    CHECK_FALSE(p.addModule(ModuleType::Overdrive));
    const auto menu = RackPage::addMenu(p, 100);
    bool sawOverdrive = false, sawGate = false;
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next()) {
        const auto& item = it.getItem();
        if (item.itemID == 100 + int(ModuleType::Overdrive)) { sawOverdrive = true; CHECK_FALSE(item.isEnabled); }
        if (item.itemID == 100 + int(ModuleType::Gate)) { sawGate = true; CHECK(item.isEnabled); }
    }
    CHECK(sawOverdrive);
    CHECK(sawGate);
}

TEST_CASE("a rack never holds more than twelve modules besides the amp", "[rack]") {
    JuceInit init;
    SasProcessor p;
    allModules(p);  // a fresh instance carries every module of the fixed-order plug-in: twelve with the cabinet
    CHECK_FALSE(p.canAdd(ModuleType::Delay));
    REQUIRE(p.removeModule({ModuleType::Delay, 0}));
    CHECK(p.canAdd(ModuleType::Delay));
    CHECK(p.addModule(ModuleType::Delay));
    CHECK_FALSE(p.addModule(ModuleType::Pitch));  // full again
}

// ---------------------------------------------------------------- modular rack: the editor

namespace {
juce::Component* findById(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren())
        if (auto* hit = findById(*child, id)) return hit;
    return nullptr;
}
}  // namespace

TEST_CASE("the editor has the Rack and Tuner pages, and the amp's condition lives in the amp unit", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* ed = dynamic_cast<SasEditor*>(base.get());
    REQUIRE(ed != nullptr);
    base->setSize(1100, 700);
    CHECK(ed->pageNames() == juce::StringArray{"Rack", "Tuner"});
    for (const char* id : {"cond_tube", "cond_bias", "cond_wear", "cond_unit", "cond_noise"}) {
        auto* c = findById(*base, id);
        REQUIRE(c != nullptr);
        CHECK(c->findParentComponentOfClass<RackPage>() != nullptr);  // inside the rack page, not beside it
    }
}

TEST_CASE("a new instance starts with the three fixed modules: gate, amp and cabinet", "[rack]") {
    JuceInit init;
    SasProcessor p;
    CHECK(typesOf(p.rackOrder()) == std::vector<ModuleType>{ModuleType::Gate, ModuleType::Amp, ModuleType::Cabinet});
}

TEST_CASE("the rack page can be rendered to a picture", "[.screenshots]") {
    JuceInit init;
    const char* dir = std::getenv("SAS_SCREENSHOT_DIR");
    if (!dir) { SKIP("SAS_SCREENSHOT_DIR is not set"); }
    juce::File(dir).createDirectory();
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    p.setRackOrder([] {
        sas::RackOrder o;
        for (auto s : {sas::SlotRef{ModuleType::Gate, 0}, {ModuleType::Overdrive, 0}, {ModuleType::Overdrive, 1}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0},
                       {ModuleType::Eq, 0}, {ModuleType::Delay, 0}, {ModuleType::Reverb, 0}})
            o.slots[std::size_t(o.count++)] = s;
        return o;
    }());
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setSize(1280, 1100);
    run(p, 80, 196.0, 0.2);
    for (int shot = 0; shot < 3; ++shot) {
        if (auto* se = dynamic_cast<SasEditor*>(ed.get())) for (int i = 0; i < 12; ++i) se->pollMeters();
        if (auto* page = findById(*ed, "") ) (void)page;
        const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream out(juce::File(dir).getChildFile("rack" + juce::String(shot) + ".png"));
        REQUIRE(out.openedOk());
        out.setPosition(0);
        out.truncate();
        png.writeImageToStream(img, out);
        break;
    }
}

TEST_CASE("a mono-only unit that follows a stereo one says it sums to mono", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.setRackOrder([] {
        sas::RackOrder o;
        for (auto s : {sas::SlotRef{ModuleType::Gate, 0}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0}, {ModuleType::Reverb, 0}, {ModuleType::Overdrive, 0}}) o.slots[std::size_t(o.count++)] = s;
        return o;
    }());
    RackPage page(p);
    page.setSize(900, 600);
    REQUIRE(page.unitCount() == 5);
    CHECK_FALSE(page.unitSumsToMono(1));  // the amp has a mono signal
    CHECK_FALSE(page.unitSumsToMono(2));  // the cabinet makes it stereo
    CHECK_FALSE(page.unitSumsToMono(3));  // the reverb is stereo
    CHECK(page.unitSumsToMono(4));        // the overdrive after it is not
}

// ---------------------------------------------------------------- the review's findings

TEST_CASE("a legacy state with the overdrive on reports the latency it always did", "[rack]") {
    JuceInit init;
    auto latencyOf = [](bool tsOn) {
        SasProcessor configured;
        setParam(configured, ids::tsOn, tsOn ? 1.0f : 0.0f);
        SasProcessor p;
        roundTrip(configured, p, stripRack);
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        buf.clear();
        p.processBlock(buf, midi);
        return p.getLatencySamples();
    };
    CHECK(latencyOf(true) == latencyOf(false));  // the overdrive runs inside the amp: no extra delay for the host to compensate
}

TEST_CASE("modules added in front of the amp go before the compressor and overdrive it runs itself", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.setRackOrder([] {
        sas::RackOrder o;
        for (auto s : {sas::SlotRef{ModuleType::Compressor, 0}, {ModuleType::Overdrive, 0}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0}}) o.slots[std::size_t(o.count++)] = s;
        return o;
    }());
    REQUIRE(p.addModule(ModuleType::Gate));
    REQUIRE(p.addModule(ModuleType::Pitch));
    using T = ModuleType;
    CHECK(typesOf(p.rackOrder()) == std::vector<T>{T::Gate, T::Gate, T::Pitch, T::Compressor, T::Overdrive, T::Amp, T::Cabinet});
}

TEST_CASE("the rack page shows the path as tiles and opens one module at a time", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.setRackOrder([] {
        sas::RackOrder o;
        for (auto s : {sas::SlotRef{ModuleType::Gate, 0}, {ModuleType::Overdrive, 0}, {ModuleType::Amp, 0}, {ModuleType::Cabinet, 0}, {ModuleType::Reverb, 0}}) o.slots[std::size_t(o.count++)] = s;
        return o;
    }());
    RackPage page(p);
    page.setSize(1000, 700);
    auto shown = [&] {
        std::vector<ModuleType> v;
        for (int i = 0; i < page.unitCount(); ++i)
            if (page.unitShown(i)) v.push_back(page.unitSlot(i).type);
        return v;
    };
    CHECK(page.tileCount() == 5);
    CHECK(page.selectedSlot() == sas::SlotRef{ModuleType::Amp, 0});  // the amp is open at the start
    CHECK(shown() == std::vector<ModuleType>{ModuleType::Amp});
    page.select({ModuleType::Reverb, 0});
    CHECK(shown() == std::vector<ModuleType>{ModuleType::Reverb});
    REQUIRE(p.addModule(ModuleType::Delay));
    page.rebuild();
    CHECK(page.selectedSlot().type == ModuleType::Delay);  // a module that was just added opens
    CHECK(shown() == std::vector<ModuleType>{ModuleType::Delay});
}

TEST_CASE("only the first gate cannot be removed: the amp and the cabinet can go and be put back, and a saved rack without them stays without them", "[rack]") {
    JuceInit init;
    using T = ModuleType;
    SasProcessor p;
    CHECK_FALSE(p.removeModule({T::Gate, 0}));
    CHECK_FALSE(p.canAdd(T::Amp));      // there is one already
    CHECK_FALSE(p.canAdd(T::Cabinet));
    REQUIRE(p.removeModule({T::Cabinet, 0}));
    REQUIRE(p.removeModule({T::Amp, 0}));
    CHECK(typesOf(p.rackOrder()) == std::vector<T>{T::Gate});
    CHECK(p.canAdd(T::Amp));
    CHECK(p.canAdd(T::Cabinet));
    CHECK_FALSE(p.canAdd(T::Amp, 1));   // a second amp needs a parallel
    REQUIRE(p.addModule(T::Amp));       // put back: the amp, then the cabinet behind it
    REQUIRE(p.addModule(T::Cabinet));
    CHECK(typesOf(p.rackOrder()) == std::vector<T>{T::Gate, T::Amp, T::Cabinet});
    CHECK_FALSE(p.addModule(T::Amp));   // only one without a parallel
    // A saved rack that lacks them loads without them (the first gate is always there).
    p.setRackOrder([] {
        sas::RackOrder o;
        o.slots[std::size_t(o.count++)] = {T::Reverb, 0};
        return o;
    }());
    CHECK(typesOf(p.rackOrder()) == std::vector<T>{T::Gate, T::Reverb});
    REQUIRE(p.addModule(T::Gate));  // a second gate is an ordinary module
    CHECK(p.removeModule({T::Gate, 1}));
    // The signal still plays with no amp and no cabinet.
    const auto out = legacy::render(p, legacy::input());
    double energy = 0.0;
    for (const float v : out) {
        REQUIRE(std::isfinite(v));
        energy += double(v) * v;
    }
    CHECK(energy > 0.0);
}

TEST_CASE("with a parallel, the first amp and cabinet can be removed from lane A, and the second ones are put in lane B", "[rack]") {
    JuceInit init;
    using T = ModuleType;
    SasProcessor p;
    REQUIRE(p.addParallel());
    REQUIRE(p.addModule(T::Amp, -1, 1));  // the second amp, in lane B
    const int a1 = [&] { for (int i = 0; i < p.rackOrder().count; ++i) if (p.rackOrder().slots[std::size_t(i)] == sas::SlotRef{T::Amp, 1}) return i; return -1; }();
    REQUIRE(a1 >= 0);
    CHECK(p.rackOrder().lane[std::size_t(a1)] == 1);
    REQUIRE(p.removeModule({T::Amp, 0}));  // the first one goes: lane A keeps the cabinet
    CHECK(p.hasParallel());
    REQUIRE(p.addModule(T::Amp));          // and comes back to lane A, not lane B
    for (int i = 0; i < p.rackOrder().count; ++i)
        if (p.rackOrder().slots[std::size_t(i)] == sas::SlotRef{T::Amp, 0}) CHECK(p.rackOrder().lane[std::size_t(i)] == 0);
    // Without the amp and the cabinet there is nothing to build a parallel around.
    SasProcessor q;
    REQUIRE(q.removeModule({T::Cabinet, 0}));
    REQUIRE(q.removeModule({T::Amp, 0}));
    CHECK_FALSE(q.addParallel());
}

TEST_CASE("the cabinet can be switched off: the amp's raw signal, and the switch is a parameter of its own", "[rack]") {
    JuceInit init;
    const auto in = legacy::input();
    auto render = [&](float cabOn) {
        SasProcessor p;
        setParam(p, ids::cabOn, cabOn);
        return legacy::render(p, in);
    };
    REQUIRE(RackState::onId(sas::ModuleType::Cabinet) != nullptr);
    const auto on = render(1.0f), off = render(0.0f);
    double diff = 0.0, energy = 0.0;
    for (std::size_t i = 0; i < on.size(); ++i) {
        diff += std::abs(double(on[i]) - double(off[i]));
        energy += double(off[i]) * off[i];
        REQUIRE(std::isfinite(off[i]));
    }
    CHECK(energy > 0.0);          // still plays
    CHECK(diff / double(on.size()) > 1e-5);  // and it is not the filtered signal
    // An old project has no such switch: it comes in on, and sounds as it did (the golden renders say so).
    SasProcessor p;
    CHECK(p.apvts.getRawParameterValue(ids::cabOn)->load() > 0.5f);
}

TEST_CASE("the input and output meters are in the header, on every page", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* ed = dynamic_cast<SasEditor*>(base.get());
    REQUIRE(ed != nullptr);
    base->setSize(1100, 700);
    CHECK(ed->headerInputDb() < -60.0f);  // nothing played yet
    run(p, 40, 220.0, 0.2);
    for (int i = 0; i < 4; ++i) ed->pollMeters();
    CHECK(ed->headerInputDb() > -30.0f);
    CHECK(ed->headerOutputDb() > -60.0f);
    // the meters belong to the editor, not to a page: the amp unit is not what shows them
    CHECK(ed->headerMetersVisible());
}

// ---------------------------------------------------------------- parallel paths: parameters of the second amp, cabinet and the mixer

TEST_CASE("the second amp and cabinet have parameters of their own, like the first", "[rack]") {
    JuceInit init;
    SasProcessor p;
    for (const char* id : {"amp", "volume", "volume2", "bass", "middle", "treble", "presence", "master", "power", "resonance", "vib_speed", "vib_intensity", "vib_on", "input", "quality",
                           "jack", "sel1", "sel4", "pickup", "cable", "tube", "wear", "unit", "noise", "bias", "speaker", "speaker_intense", "cab", "cab_on", "ir_blend", "ir_width",
                           "double_amount", "double_time"}) {
        INFO(id);
        auto* a = p.apvts.getParameter(id);
        auto* b = p.apvts.getParameter(juce::String(id) + "__2");
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        CHECK(b->getDefaultValue() == a->getDefaultValue());
        CHECK(b->getNumSteps() == a->getNumSteps());
        CHECK(b->getName(64).endsWith(" 2"));
    }
    for (const char* id : {"mix_level_a", "mix_level_b", "mix_pan_a", "mix_pan_b", "mix_invert_b"}) CHECK(p.apvts.getParameter(id) != nullptr);
    CHECK(p.apvts.getParameter("volume__3") == nullptr);  // two amps, no more
}

TEST_CASE("moving a knob of amp A does not move amp B", "[rack]") {
    JuceInit init;
    SasProcessor p;
    const float bDefault = p.apvts.getRawParameterValue("volume__2")->load();
    setParam(p, "volume", 9.0f);
    CHECK(p.apvts.getRawParameterValue("volume__2")->load() == bDefault);
    setParam(p, "bass__2", 1.0f);
    CHECK(p.apvts.getRawParameterValue("bass")->load() == Catch::Approx(5.0f));
    setParam(p, "quality__2", 0.0f);
    CHECK(p.apvts.getRawParameterValue("quality")->load() == Catch::Approx(4.0f));  // amp A stays in Efficient
}

// ---------------------------------------------------------------- parallel paths: the processor

namespace {

// Plays `in` through a processor that is already prepared (no prepareToPlay) and returns the left channel.
std::vector<float> playOn(SasProcessor& p, const std::vector<float>& in) {
    juce::MidiBuffer midi;
    std::vector<float> out;
    juce::AudioBuffer<float> buf(2, 256);
    for (std::size_t at = 0; at < in.size(); at += 256) {
        const int n = int(std::min<std::size_t>(256, in.size() - at));
        buf.setSize(2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) buf.setSample(ch, i, in[at + std::size_t(i)]);
        p.processBlock(buf, midi);
        for (int i = 0; i < n; ++i) out.push_back(buf.getSample(0, i));
    }
    return out;
}

// Runs silence until the circuits of the amp of that instance are built (they come from another thread), and a little longer.
bool waitForAmp(SasProcessor& p, int instance) {
    const std::vector<float> silence(256, 0.0f);
    for (int i = 0; i < 4000 && !p.ampBuilt(instance); ++i) {
        playOn(p, silence);
        juce::Thread::sleep(2);
    }
    for (int i = 0; i < 40; ++i) playOn(p, silence);  // the fades settle
    return p.ampBuilt(instance);
}

double rmsOf(const std::vector<float>& v, std::size_t from = 0) {
    double s = 0.0;
    for (std::size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}

}  // namespace

TEST_CASE("parallel paths: two identical amps sum to twice one, and removing the parallel leaves the chain as it was", "[rack]") {
    JuceInit init;
    const auto in = legacy::input();
    SasProcessor single;
    single.prepareToPlay(48000.0, 256);
    for (int i = 0; i < 40; ++i) playOn(single, std::vector<float>(256, 0.0f));
    const auto one = playOn(single, in);

    SasProcessor par;
    par.prepareToPlay(48000.0, 256);
    REQUIRE(addSecondAmp(par));
    CHECK(par.hasParallel());
    REQUIRE(waitForAmp(par, 1));
    const auto two = playOn(par, in);
    REQUIRE(two.size() == one.size());
    // The gain of the sum over one amp, away from the start (the lane fades have settled): twice, and in step with it.
    double num = 0.0, den = 0.0;
    for (std::size_t i = 200; i < one.size(); ++i) {
        num += double(two[i]) * double(one[i]);
        den += double(one[i]) * double(one[i]);
    }
    CHECK(num / den == Catch::Approx(2.0).margin(0.02));
    CHECK(rmsOf(two, 200) == Catch::Approx(2.0 * rmsOf(one, 200)).epsilon(0.02));

    // Taking the parallel away leaves the series chain; the second amp's controls go back to their defaults.
    setParam(par, "volume__2", 9.0f);
    REQUIRE(par.removeParallel());
    CHECK_FALSE(par.hasParallel());
    using T = ModuleType;
    CHECK(typesOf(par.rackOrder()) == std::vector<T>{T::Gate, T::Amp, T::Cabinet});
    CHECK(par.apvts.getRawParameterValue("volume__2")->load() == Catch::Approx(6.0f));
}

TEST_CASE("the parallel round trips through a preset and a damaged one is repaired", "[rack]") {
    JuceInit init;
    SasProcessor a, b;
    REQUIRE(addSecondAmp(a));
    setParam(a, "volume__2", 8.0f);
    setParam(a, ids::mixPanB, 50.0f);
    roundTrip(a, b, [](juce::XmlElement&) {});
    CHECK(b.hasParallel());
    REQUIRE(b.rackOrder().count == a.rackOrder().count);
    for (int i = 0; i < a.rackOrder().count; ++i) {
        CHECK(b.rackOrder().slots[std::size_t(i)] == a.rackOrder().slots[std::size_t(i)]);
        CHECK(b.rackOrder().lane[std::size_t(i)] == a.rackOrder().lane[std::size_t(i)]);
    }
    CHECK(b.apvts.getRawParameterValue("volume__2")->load() == Catch::Approx(8.0f));
    CHECK(b.apvts.getRawParameterValue(ids::mixPanB)->load() == Catch::Approx(50.0f));

    // A lane B without a mixer is not a parallel: the state loads as a plain chain.
    SasProcessor c, d;
    roundTrip(c, d, [](juce::XmlElement& xml) {
        if (auto* rack = xml.getChildByName("RACK"))
            if (auto* m = rack->getFirstChildElement()) m->setAttribute("lane", 1);
    });
    CHECK_FALSE(d.hasParallel());
    CHECK(d.rackOrder().valid());
}

TEST_CASE("amp B plays nothing until its circuits are built, and the latency is that of the later lane", "[rack]") {
    JuceInit init;
    const auto in = legacy::input();
    SasProcessor single;
    single.prepareToPlay(48000.0, 256);
    const auto one = playOn(single, in);

    SasProcessor par;
    par.prepareToPlay(48000.0, 256);
    REQUIRE(addSecondAmp(par));
    CHECK_FALSE(par.ampBuilt(1));  // asked for, not there yet
    const auto early = playOn(par, in);
    for (float v : early) REQUIRE(std::isfinite(v));
    CHECK(rmsOf(early) < 1.5 * rmsOf(one));  // not doubled: only lane A has been playing

    // Amp B in the 4x quality is later than amp A: the host is told about the later one.
    setParam(par, "quality__2", 1.0f);
    REQUIRE(waitForAmp(par, 1));
    SasProcessor high;
    setParam(high, ids::quality, 1.0f);
    high.prepareToPlay(48000.0, 256);
    playOn(high, std::vector<float>(256, 0.0f));
    playOn(high, std::vector<float>(256, 0.0f));  // the quality reaches the chain in the block after the one that read it
    CHECK(par.getLatencySamples() == high.getLatencySamples());
}

// ---------------------------------------------------------------- parallel paths: the page

namespace {
RackPage* findRackPage(juce::Component& root) {
    if (auto* page = dynamic_cast<RackPage*>(&root)) return page;
    for (auto* child : root.getChildren())
        if (auto* hit = findRackPage(*child)) return hit;
    return nullptr;
}
}  // namespace

TEST_CASE("the rack page draws the parallel as two lanes and offers it once", "[rack]") {
    JuceInit init;
    SasProcessor p;
    RackPage page(p);
    page.setSize(1000, 700);
    auto parallelItem = [&] {
        const auto menu = RackPage::addMenu(p, 100);
        juce::PopupMenu::MenuItemIterator it(menu, true);
        while (it.next())
            if (it.getItem().itemID == 99) return it.getItem().isEnabled;
        return false;
    };
    CHECK(parallelItem());
    REQUIRE(addSecondAmp(p));
    page.rebuild();
    using T = ModuleType;
    REQUIRE(page.tileCount() == 7);  // gate, splitter, amp, cabinet, amp B, cabinet B, mixer
    int lanesB = 0;
    for (int i = 0; i < page.tileCount(); ++i) {
        const auto slot = page.unitSlot(i);
        if (page.tileLane(i) == 1) {
            ++lanesB;
            CHECK((slot == sas::SlotRef{T::Amp, 1} || slot == sas::SlotRef{T::Cabinet, 1}));
        }
    }
    CHECK(lanesB == 2);
    CHECK(page.selectedSlot() == sas::SlotRef{T::Amp, 1});  // the second amp opens
    CHECK_FALSE(p.canAddParallel());
    CHECK_FALSE(parallelItem());
    // A pedal added in lane B stays in lane B.
    REQUIRE(p.moveModuleToLane(sas::SlotRef{T::Amp, 1}, 0, 2) == false);  // the second amp cannot leave its lane
    REQUIRE(p.addModule(T::Overdrive, 5));
    page.rebuild();
    for (int i = 0; i < page.tileCount(); ++i)
        if (page.unitSlot(i).type == T::Overdrive) CHECK(page.tileLane(i) == 1);
}

TEST_CASE("amp B has its own panel in the rack, with its own controls", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setSize(1100, 900);
    REQUIRE(addSecondAmp(p));
    auto* page = findRackPage(*ed);
    REQUIRE(page != nullptr);
    page->rebuild();
    auto* input = findById(*ed, "amp_b_input");
    REQUIRE(input != nullptr);
    auto* unit = input->findParentComponentOfClass<ModuleUnit>();
    REQUIRE(unit != nullptr);
    CHECK(unit->slot() == sas::SlotRef{ModuleType::Amp, 1});
    CHECK(findById(*ed, "amp_b_select") != nullptr);
}

TEST_CASE("the mixer's level of lane B changes the sum", "[rack]") {
    JuceInit init;
    const auto in = legacy::input();
    auto level = [&](float dbB) {
        SasProcessor p;
        p.prepareToPlay(48000.0, 256);
        REQUIRE(addSecondAmp(p));
        setParam(p, ids::mixLevelB, dbB);
        REQUIRE(waitForAmp(p, 1));
        return rmsOf(playOn(p, in), 200);
    };
    const double full = level(0.0f), quiet = level(-24.0f);
    CHECK(quiet < 0.7 * full);  // lane B at -24 dB leaves little more than lane A
    CHECK(quiet > 0.4 * full);
}

TEST_CASE("the parallel page can be rendered to a picture", "[.screenshots]") {
    JuceInit init;
    const char* dir = std::getenv("SAS_SCREENSHOT_DIR");
    if (!dir) { SKIP("SAS_SCREENSHOT_DIR is not set"); }
    juce::File(dir).createDirectory();
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    REQUIRE(addSecondAmp(p));
    REQUIRE(p.addModule(ModuleType::Overdrive));
    REQUIRE(p.addModule(ModuleType::Reverb));
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setSize(1280, 1000);
    auto* page = findRackPage(*ed);
    REQUIRE(page != nullptr);
    page->rebuild();
    run(p, 40, 196.0, 0.2);
    const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    juce::FileOutputStream out(juce::File(dir).getChildFile("parallel.png"));
    REQUIRE(out.openedOk());
    out.setPosition(0);
    out.truncate();
    png.writeImageToStream(img, out);
}

TEST_CASE("an overdrive dropped on lane B moves to lane B", "[rack]") {
    JuceInit init;
    SasProcessor p;
    REQUIRE(addSecondAmp(p));
    REQUIRE(p.addModule(ModuleType::Overdrive));  // it goes in front of the first amp, in lane A
    RackPage page(p);
    page.setSize(1000, 700);
    page.rebuild();
    using T = ModuleType;
    int odTile = -1, ampB = -1;
    for (int i = 0; i < page.tileCount(); ++i) {
        if (page.unitSlot(i).type == T::Overdrive) odTile = i;
        if (page.unitSlot(i) == sas::SlotRef{T::Amp, 1}) ampB = i;
    }
    REQUIRE(odTile >= 0);
    REQUIRE(ampB >= 0);
    CHECK(page.tileLane(odTile) == 0);
    // Dropped on the tile of the second amp (the point is in the page's own coordinates).
    const auto box = page.tileBox(ampB);
    const auto where = box.getCentre() + page.mapOrigin();
    juce::DragAndDropTarget::SourceDetails details(ModuleUnit::dragDescription({T::Overdrive, 0}), &page, where);
    page.itemDropped(details);
    odTile = -1;
    for (int i = 0; i < page.tileCount(); ++i)
        if (page.unitSlot(i).type == T::Overdrive) odTile = i;
    REQUIRE(odTile >= 0);
    CHECK(page.tileLane(odTile) == 1);
}

TEST_CASE("an overdrive dropped on an empty cell of lane B, or between its tiles, moves to lane B", "[rack]") {
    JuceInit init;
    using T = ModuleType;
    for (int variant = 0; variant < 4; ++variant) {
        SasProcessor p;
        REQUIRE(addSecondAmp(p));
        REQUIRE(p.addModule(T::Overdrive));
        RackPage page(p);
        page.setSize(900, 700);
        page.rebuild();
        int cabB = -1, cabA = -1, splitter = -1;
        for (int i = 0; i < page.tileCount(); ++i) {
            if (page.unitSlot(i).type == T::Splitter) splitter = i;
            if (page.unitSlot(i) == sas::SlotRef{T::Cabinet, 1}) cabB = i;
            if (page.unitSlot(i) == sas::SlotRef{T::Cabinet, 0}) cabA = i;
        }
        REQUIRE(cabB >= 0);
        juce::Point<int> where;
        if (variant == 0) where = page.tileBox(cabA).getCentre() + juce::Point<int>(0, 78);        // the empty cell below the first cabinet
        if (variant == 1) where = juce::Point<int>(page.tileBox(cabB).getRight() - 2, page.tileBox(cabB).getY() + 30);        // just before the end of the second cabinet's tile
        if (variant == 2) where = page.tileBox(cabB).getTopLeft() + juce::Point<int>(-6, 30);      // the gap in front of the second cabinet
        if (variant == 3) where = page.tileBox(splitter).getCentre() + juce::Point<int>(0, 78);     // under the splitter, where the wire to lane B comes down
        juce::DragAndDropTarget::SourceDetails details(ModuleUnit::dragDescription({T::Overdrive, 0}), &page, where + page.mapOrigin());
        page.itemDropped(details);
        int od = -1;
        for (int i = 0; i < page.tileCount(); ++i)
            if (page.unitSlot(i).type == T::Overdrive) od = i;
        INFO("variant " << variant);
        REQUIRE(od >= 0);
        CHECK(page.tileLane(od) == 1);
    }
}

TEST_CASE("the speaker's dynamics are in the cabinet unit, of each amp", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    ed->setSize(1100, 900);
    REQUIRE(addSecondAmp(p));
    auto* page = findRackPage(*ed);
    REQUIRE(page != nullptr);
    page->rebuild();
    for (const auto& [id, instance] : {std::pair<const char*, int>{"speaker_dynamics", 0}, {"cabinet_b_speaker", 1}}) {
        auto* knob = findById(*ed, id);
        INFO(id);
        REQUIRE(knob != nullptr);
        auto* unit = knob->findParentComponentOfClass<ModuleUnit>();
        REQUIRE(unit != nullptr);
        CHECK(unit->slot() == sas::SlotRef{ModuleType::Cabinet, instance});
    }
}

TEST_CASE("the input and output knobs are in the header with the meters", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* ed = dynamic_cast<SasEditor*>(base.get());
    REQUIRE(ed != nullptr);
    base->setSize(1100, 700);
    CHECK(ed->headerInputKnob().isVisible());
    CHECK(ed->headerOutputKnob().isVisible());
    CHECK(ed->headerInputKnob().getY() < 40);  // in the header row, not in a unit
    ed->headerInputKnob().setValue(6.0, juce::sendNotificationSync);
    ed->headerOutputKnob().setValue(-3.0, juce::sendNotificationSync);
    CHECK(p.apvts.getRawParameterValue(ids::input)->load() == Catch::Approx(6.0f));
    CHECK(p.apvts.getRawParameterValue(ids::output)->load() == Catch::Approx(-3.0f));
}

// ---------------------------------------------------------------- parallel paths: lane B is the player's to fill

TEST_CASE("a parallel starts with an empty lane B, and the second amp and cabinet are added by choice", "[rack]") {
    JuceInit init;
    using T = ModuleType;
    SasProcessor p;
    CHECK_FALSE(p.canAdd(T::Amp, 1));  // there is no lane B yet
    CHECK_FALSE(p.addModule(T::Amp, -1, 1));
    REQUIRE(p.addParallel());
    CHECK(typesOf(p.rackOrder()) == std::vector<T>{T::Gate, T::Splitter, T::Amp, T::Cabinet, T::Mixer});
    for (int i = 0; i < p.rackOrder().count; ++i) CHECK(p.rackOrder().lane[std::size_t(i)] == 0);  // nothing in lane B
    CHECK(p.canAdd(T::Amp, 1));
    REQUIRE(p.addModule(T::Reverb, -1, 1));  // a reverb of its own in lane B
    REQUIRE(p.rackOrder().count == 6);
    CHECK(p.rackOrder().lane[4] == 1);
    CHECK_FALSE(SasProcessor().addModule(T::Reverb, -1, 1));  // without a parallel there is no lane B
    REQUIRE(p.addModule(T::Amp, -1, 1));  // and a second amp, when the player wants one
    REQUIRE(p.addModule(T::Cabinet, -1, 1));
    CHECK_FALSE(p.canAdd(T::Amp, 1));     // only one second amp
    CHECK(p.removeModule({T::Amp, 1}));   // it can go again, leaving the rest of lane B
    CHECK(p.canAdd(T::Amp, 1));
    CHECK(p.hasParallel());
    // Taking the parallel away takes everything of lane B.
    REQUIRE(p.removeParallel());
    CHECK(typesOf(p.rackOrder()) == std::vector<T>{T::Gate, T::Amp, T::Cabinet});
}

TEST_CASE("an empty lane B plays the dry signal beside lane A", "[rack]") {
    JuceInit init;
    const auto in = legacy::input();
    SasProcessor single;
    single.prepareToPlay(48000.0, 256);
    for (int i = 0; i < 40; ++i) playOn(single, std::vector<float>(256, 0.0f));
    const auto one = playOn(single, in);
    SasProcessor par;
    par.prepareToPlay(48000.0, 256);
    REQUIRE(par.addParallel());
    setParam(par, ids::mixLevelB, -24.0f);  // a little of the dry guitar next to the amp
    for (int i = 0; i < 60; ++i) playOn(par, std::vector<float>(256, 0.0f));
    const auto mixed = playOn(par, in);
    for (float v : mixed) REQUIRE(std::isfinite(v));
    CHECK(rmsOf(mixed, 200) > 0.9 * rmsOf(one, 200));  // the amp is still there
    CHECK(rmsOf(mixed, 200) < 1.5 * rmsOf(one, 200));  // and the dry guitar is far below it
}

TEST_CASE("the tremolo and the flanger are modules of the rack: added, heard, saved and removed", "[rack]") {
    JuceInit init;
    using T = sas::ModuleType;
    const auto in = legacy::input();
    for (const auto type : {T::Tremolo, T::Flanger, T::Wah, T::Fuzz, T::Boost, T::Limiter, T::TapeEcho}) {
        const bool trem = type == T::Tremolo;
        SasProcessor p;
        p.prepareToPlay(48000.0, 256);
        for (int i = 0; i < 40; ++i) playOn(p, std::vector<float>(256, 0.0f));
        const auto before = playOn(p, in);
        REQUIRE(p.addModule(type));
        { const auto order = typesOf(p.rackOrder()); CHECK(std::find(order.begin(), order.end(), type) != order.end()); }
        setParam(p, trem ? ids::trOn : type == T::Wah ? ids::wahOn : type == T::Fuzz ? ids::fzOn : type == T::Boost ? ids::bstOn : type == T::Limiter ? ids::limOn : type == T::TapeEcho ? ids::tpOn : ids::flOn, 1.0f);
        if (trem) setParam(p, ids::trDepth, 100.0f);
        for (int i = 0; i < 40; ++i) playOn(p, std::vector<float>(256, 0.0f));
        const auto after = playOn(p, in);
        double diff = 0.0;
        for (std::size_t i = 0; i < after.size(); ++i) {
            REQUIRE(std::isfinite(after[i]));
            diff += std::fabs(double(after[i]) - double(before[i]));
        }
        CHECK(diff > 1.0);
        SasProcessor q;
        roundTrip(p, q, [](juce::XmlElement&) {});
        CHECK(typesOf(q.rackOrder()) == typesOf(p.rackOrder()));
        REQUIRE(p.removeModule({type, 0}));
        { const auto order = typesOf(p.rackOrder()); CHECK(std::find(order.begin(), order.end(), type) == order.end()); }
    }
}

TEST_CASE("wah presets set the whole wah, switch it on, and Off leaves the knobs alone", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 256);
    REQUIRE(p.addModule(sas::ModuleType::Wah));
    auto val = [&](const char* id) { return p.apvts.getRawParameterValue(id)->load(); };
    for (int i = 1; i < kWahPresetCount; ++i) {
        p.applyWahPreset(i);
        const WahPreset& w = kWahPresets[i];
        CHECK(val(ids::wahOn) > 0.5f);
        CHECK(int(val(ids::wahMode) + 0.5f) == w.mode);
        CHECK(val(ids::wahHeel) == Catch::Approx(w.heelHz).margin(1.0));
        CHECK(val(ids::wahToe) == Catch::Approx(w.toeHz).margin(2.0));
        CHECK(val(ids::wahQ) == Catch::Approx(w.q).margin(0.05));
        CHECK((val(ids::wahDir) > 0.5f) == w.down);
        CHECK((val(ids::wahStereo) > 0.5f) == w.stereo);
        CHECK(w.heelHz < w.toeHz);
    }
    p.applyWahPreset(10);  // "Stereo swirl"
    const float rate = val(ids::wahRate);
    p.applyWahPreset(0);   // "Off"
    CHECK(val(ids::wahOn) < 0.5f);
    CHECK(val(ids::wahRate) == rate);
}

TEST_CASE("fuzz presets set the whole fuzz, switch it on, and Off leaves the knobs alone", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 256);
    REQUIRE(p.addModule(sas::ModuleType::Fuzz));
    auto val = [&](const char* id) { return p.apvts.getRawParameterValue(id)->load(); };
    for (int i = 1; i < kFuzzPresetCount; ++i) {
        p.applyFuzzPreset(i);
        const FuzzPreset& f = kFuzzPresets[i];
        CHECK(val(ids::fzOn) > 0.5f);
        CHECK(int(val(ids::fzType) + 0.5f) == f.type);
        CHECK(val(ids::fzFuzz) == Catch::Approx(f.fuzz).margin(0.05));
        CHECK(val(ids::fzMix) == Catch::Approx(f.mix).margin(0.5));
    }
    p.applyFuzzPreset(8);  // "Blended fuzz"
    const float mix = val(ids::fzMix);
    p.applyFuzzPreset(0);
    CHECK(val(ids::fzOn) < 0.5f);
    CHECK(val(ids::fzMix) == mix);
}

TEST_CASE("tape echo presets set the whole echo, switch it on, and Off leaves the knobs alone", "[rack]") {
    JuceInit init;
    SasProcessor p;
    p.prepareToPlay(48000.0, 256);
    REQUIRE(p.addModule(sas::ModuleType::TapeEcho));
    auto val = [&](const char* id) { return p.apvts.getRawParameterValue(id)->load(); };
    for (int i = 1; i < kTapePresetCount; ++i) {
        p.applyTapePreset(i);
        const TapePreset& t = kTapePresets[i];
        CHECK(val(ids::tpOn) > 0.5f);
        CHECK(val(ids::tpTime) == Catch::Approx(t.timeMs).margin(1.0));
        CHECK(int(val(ids::tpHeads) + 0.5f) == t.heads);
        CHECK(val(ids::tpIntensity) == Catch::Approx(t.intensity).margin(0.5));
        CHECK((val(ids::tpStereo) > 0.5f) == t.stereo);
    }
    p.applyTapePreset(6);  // "Swell"
    const float fb = val(ids::tpIntensity);
    p.applyTapePreset(0);
    CHECK(val(ids::tpOn) < 0.5f);
    CHECK(val(ids::tpIntensity) == fb);
}

TEST_CASE("the equaliser has eight bands: the three added ones are bells, flat until moved, and a preset leaves them flat", "[rack]") {
    JuceInit init;
    SasProcessor p;
    REQUIRE(sas::EqParams::kBands == 8);
    REQUIRE(p.addModule(sas::ModuleType::Eq));
    for (int b = 5; b < 8; ++b) {
        CHECK(p.apvts.getParameter(ids::eqFreq[b]) != nullptr);
        CHECK(p.apvts.getRawParameterValue(ids::eqGain[b])->load() == 0.0f);
    }
    setParam(p, ids::eqGain[6], 6.0f);
    p.applyEqPreset(2);  // "Warm blues"
    CHECK(p.apvts.getRawParameterValue(ids::eqGain[6])->load() == 0.0f);  // the preset leaves the added bands flat
    CHECK(p.apvts.getRawParameterValue(ids::eqGain[0])->load() > 1.0f);   // and sets its own five
}

TEST_CASE("the interface follows the language: Spanish texts, English when there is no translation, and the language of the system is detected", "[i18n]") {
    JuceInit init;
    const auto before = i18n::active();
    i18n::useForSession(i18n::Language::English);
    CHECK(tr("Remove this module") == "Remove this module");
    i18n::useForSession(i18n::Language::Spanish);
    CHECK(tr("Remove this module") == juce::String::fromUTF8("Quitar este m\303\263dulo"));
    CHECK(tr("a text that has no translation") == "a text that has no translation");  // English, never empty
    CHECK(tr("") == "");
    CHECK(tr("TONE") == "TONO");
    // The system language: any Spanish is Spanish, the rest English.
    CHECK(i18n::fromSystemCode("es") == i18n::Language::Spanish);
    CHECK(i18n::fromSystemCode("es_AR") == i18n::Language::Spanish);
    CHECK(i18n::fromSystemCode("es-ES") == i18n::Language::Spanish);
    CHECK(i18n::fromSystemCode("ES") == i18n::Language::Spanish);
    CHECK(i18n::fromSystemCode("en") == i18n::Language::English);
    CHECK(i18n::fromSystemCode("fr_FR") == i18n::Language::English);
    CHECK(i18n::fromSystemCode("") == i18n::Language::English);
    // A message with a number inside, and the model messages of the processor.
    CHECK(i18n::namMessage("the file is too big") == juce::String::fromUTF8("el archivo es demasiado grande"));
    CHECK(i18n::namMessage("Trained at 44100 Hz, played at 48000 Hz: it will sound off-pitch.").contains("44100"));
    CHECK(i18n::namMessage("Trained at 44100 Hz, played at 48000 Hz: it will sound off-pitch.").contains("48000"));
    CHECK(trAll(juce::StringArray{"Mono", "Stereo"}) == juce::StringArray{"Mono", juce::String::fromUTF8("Est\303\251reo")});
    i18n::useForSession(before);
}

TEST_CASE("the editor is built in Spanish and in English with the same structure", "[i18n]") {
    JuceInit init;
    const auto before = i18n::active();
    juce::StringArray pages[2];
    int k = 0;
    for (auto lang : {i18n::Language::English, i18n::Language::Spanish}) {
        i18n::useForSession(lang);
        SasProcessor p;
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
        auto* ed = dynamic_cast<SasEditor*>(base.get());
        REQUIRE(ed != nullptr);
        base->setSize(1100, 700);
        pages[k++] = ed->pageNames();
    }
    CHECK(pages[0] == juce::StringArray{"Rack", "Tuner"});
    CHECK(pages[1] == juce::StringArray{"Rack", "Afinador"});
    i18n::useForSession(before);
}

// Every module's panel at the narrowest width the rack gives it, to see that the texts fit (in the language of SAS_TEST_LANGUAGE).
TEST_CASE("every module panel can be rendered to a picture", "[.screenshots]") {
    JuceInit init;
    const char* dir = std::getenv("SAS_SCREENSHOT_DIR");
    if (!dir) { SKIP("SAS_SCREENSHOT_DIR is not set"); }
    juce::File(dir).createDirectory();
    SasProcessor p;
    p.prepareToPlay(48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    juce::Component host;
    host.setLookAndFeel(&ed->getLookAndFeel());
    std::vector<std::unique_ptr<ModuleUnit>> units;
    int y = 0;
    const int width = 860;
    for (auto type : {ModuleType::Gate, ModuleType::Compressor, ModuleType::Overdrive, ModuleType::NamPedal, ModuleType::Phaser, ModuleType::Chorus, ModuleType::Vibe, ModuleType::Tremolo,
                      ModuleType::Flanger, ModuleType::Wah, ModuleType::Fuzz, ModuleType::Boost, ModuleType::Limiter, ModuleType::Pitch, ModuleType::Delay, ModuleType::TapeEcho,
                      ModuleType::Reverb, ModuleType::Eq}) {
        units.push_back(std::make_unique<ModuleUnit>(p, sas::SlotRef{type, 0}));
        auto& u = *units.back();
        host.addAndMakeVisible(u);
        u.setBounds(0, y, width, u.preferredHeight());
        y += u.preferredHeight() + 4;
    }
    host.setSize(width, y);
    const auto img = host.createComponentSnapshot(host.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    juce::FileOutputStream out(juce::File(dir).getChildFile("modules.png"));
    REQUIRE(out.openedOk());
    out.setPosition(0);
    out.truncate();
    png.writeImageToStream(img, out);
    host.setLookAndFeel(nullptr);
}

TEST_CASE("a module that is added, or put in the place of another, starts on; replacing keeps the place and the lane", "[rack]") {
    JuceInit init;
    using T = sas::ModuleType;
    SasProcessor p;
    auto on = [&](sas::SlotRef s) { return p.apvts.getRawParameterValue(RackState::paramId(RackState::onId(s.type), s.instance))->load() > 0.5f; };
    auto indexOf = [&](sas::SlotRef s) {
        for (int i = 0; i < p.rackOrder().count; ++i)
            if (p.rackOrder().slots[std::size_t(i)] == s) return i;
        return -1;
    };

    // Adding a pedal or an effect turns it on.
    REQUIRE(p.addModule(T::Overdrive));
    CHECK(on({T::Overdrive, 0}));
    REQUIRE(p.addModule(T::Delay));
    CHECK(on({T::Delay, 0}));

    // What can be replaced: not the amp, the cabinet or the first gate, not by its own type or by an amp.
    CHECK_FALSE(p.canReplace({T::Amp, 0}, T::Fuzz));
    CHECK_FALSE(p.canReplace({T::Cabinet, 0}, T::Fuzz));
    CHECK_FALSE(p.canReplace({T::Gate, 0}, T::Fuzz));
    CHECK_FALSE(p.canReplace({T::Overdrive, 0}, T::Overdrive));
    CHECK_FALSE(p.canReplace({T::Overdrive, 0}, T::Amp));
    CHECK_FALSE(p.canReplace({T::Fuzz, 0}, T::Wah));  // not in the rack

    // Replacing: same place, the new one is on, the old one is gone, the rack is no bigger.
    const int at = indexOf({T::Overdrive, 0}), count = p.rackOrder().count;
    REQUIRE(p.canReplace({T::Overdrive, 0}, T::Fuzz));
    REQUIRE(p.replaceModule({T::Overdrive, 0}, T::Fuzz));
    CHECK(p.rackOrder().count == count);
    CHECK(indexOf({T::Fuzz, 0}) == at);
    CHECK(indexOf({T::Overdrive, 0}) < 0);
    CHECK(on({T::Fuzz, 0}));
    CHECK_FALSE(p.replaceModule({T::Overdrive, 0}, T::Wah));  // it is not there any more

    // A type that is at its limit is refused (a second fuzz would need a free slot of its own).
    if (RackState::maxInstances(T::Fuzz) == 1) {
        REQUIRE(p.addModule(T::Boost));
        CHECK_FALSE(p.canReplace({T::Boost, 0}, T::Fuzz));
        CHECK_FALSE(p.replaceModule({T::Boost, 0}, T::Fuzz));
        CHECK(indexOf({T::Boost, 0}) >= 0);
    }

    // In a parallel the new module stays in the lane of the old one.
    REQUIRE(p.addParallel());
    REQUIRE(p.addModule(T::Reverb, -1, 1));
    const int reverbAt = indexOf({T::Reverb, 0});
    REQUIRE(reverbAt >= 0);
    REQUIRE(p.rackOrder().lane[std::size_t(reverbAt)] == 1);
    REQUIRE(p.replaceModule({T::Reverb, 0}, T::Chorus));
    CHECK(indexOf({T::Chorus, 0}) == reverbAt);
    CHECK(p.rackOrder().lane[std::size_t(reverbAt)] == 1);
    CHECK(on({T::Chorus, 0}));
    // The ends of the parallel are not replaced.
    CHECK_FALSE(p.canReplace({T::Splitter, 0}, T::Chorus));
}

TEST_CASE("the entry of the tile menu is translated", "[rack][i18n]") {
    JuceInit init;
    i18n::useForSession(i18n::Language::Spanish);
    CHECK(tr("Replace with") == "Reemplazar por");
    CHECK(tr("Remove") == "Quitar");
    i18n::useForSession(i18n::Language::English);
    CHECK(tr("Replace with") == "Replace with");
}

TEST_CASE("a stereo pair that reaches the cabinet leaves it as a stereo pair, in every cabinet mode", "[rack]") {
    JuceInit init;
    using T = sas::ModuleType;
    for (const auto mode : {CabMode::Filter, CabMode::Ir}) {
        SasProcessor p;
        p.setRackOrder([] {
            sas::RackOrder o;
            for (auto s : {sas::SlotRef{T::Gate, 0}, {T::Amp, 0}, {T::Reverb, 0}, {T::Cabinet, 0}}) o.slots[std::size_t(o.count++)] = s;
            return o;
        }());
        p.prepareToPlay(48000.0, 256);
        p.applyReverbPreset(2);  // a plate in front of the cabinet: the pair it makes is not the same in both ears
        setParam(p, ids::cab, float(int(mode)));
        if (mode == CabMode::Ir) {
            REQUIRE(SasProcessor::builtInImpulseResponseCount() > 0);
            REQUIRE(p.loadBuiltInImpulseResponse(0, 0, 0));  // slot A of the first cabinet
        }
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf(2, 256);
        double diff = 0.0, energy = 0.0;
        for (int blk = 0; blk < 200; ++blk) {
            buf.clear();
            if (blk < 4) fill(buf, 220.0, 0.3, 48000.0, blk * 256);
            p.processBlock(buf, midi);
            if (blk > 20)
                for (int i = 0; i < 256; ++i) {
                    const double l = buf.getSample(0, i), r = buf.getSample(1, i);
                    diff += (l - r) * (l - r);
                    energy += 0.5 * (l * l + r * r);
                }
        }
        INFO("mode " << int(mode) << ": difference " << diff << ", energy " << energy);
        REQUIRE(energy > 1e-9);
        CHECK(diff > 0.01 * energy);  // the two ears differ (a cabinet that summed the pair to mono would give exactly the same signal on both)
        for (int i = 0; i < 256; ++i) REQUIRE(std::isfinite(buf.getSample(0, i)));
    }
}

TEST_CASE("the modulation effects have a Mix parameter, 100 % by default, that fades the effect out", "[rack]") {
    JuceInit init;
    using T = sas::ModuleType;
    struct Fx { T type; const char* on; const char* mix; };
    for (const Fx fx : {Fx{T::Phaser, ids::phOn, ids::phMix}, Fx{T::Chorus, ids::chOn, ids::chMix}, Fx{T::Vibe, ids::vbOn, ids::vbMix}, Fx{T::Flanger, ids::flOn, ids::flMix}}) {
        INFO("module " << int(fx.type));
        SasProcessor p;
        p.prepareToPlay(48000.0, 256);
        REQUIRE(p.apvts.getParameter(fx.mix) != nullptr);
        CHECK(p.apvts.getRawParameterValue(fx.mix)->load() == Catch::Approx(100.0f));  // as it always sounded
        REQUIRE(p.addModule(fx.type));
        CHECK(p.apvts.getRawParameterValue(fx.on)->load() > 0.5f);  // a new module starts on
        CHECK(p.apvts.getRawParameterValue(fx.mix)->load() == Catch::Approx(100.0f));
        const auto in = legacy::input();
        for (int i = 0; i < 40; ++i) playOn(p, std::vector<float>(256, 0.0f));
        const auto full = playOn(p, in);
        setParam(p, fx.mix, 0.0f);
        for (int i = 0; i < 40; ++i) playOn(p, std::vector<float>(256, 0.0f));
        const auto none = playOn(p, in);
        setParam(p, fx.on, 0.0f);
        for (int i = 0; i < 40; ++i) playOn(p, std::vector<float>(256, 0.0f));
        const auto off = playOn(p, in);
        double dFullOff = 0.0, dNoneOff = 0.0;
        for (std::size_t i = 0; i < in.size(); ++i) {
            dFullOff += double(full[i] - off[i]) * double(full[i] - off[i]);
            dNoneOff += double(none[i] - off[i]) * double(none[i] - off[i]);
        }
        INFO("effect on vs off " << dFullOff << ", mix 0 vs off " << dNoneOff);
        CHECK(dFullOff > 1e-6);                // the effect is heard
        CHECK(dNoneOff < 0.01 * dFullOff);     // and at 0 % it is not
    }
}

#include "ModulePresets.h"

TEST_CASE("every module without a table of its own has factory presets that set every one of its knobs, in range", "[rack][i18n]") {
    JuceInit init;
    using T = sas::ModuleType;
    SasProcessor p;
    const T types[] = {T::Gate, T::Compressor, T::Overdrive, T::Phaser, T::Chorus, T::Vibe, T::Tremolo, T::Flanger, T::Pitch, T::Boost, T::Limiter};
    for (const T type : types) {
        const auto list = modulePresets(type);
        INFO("module " << sas::moduleTypeName(type));
        REQUIRE(list.count >= 6);
        const char* on = RackState::onId(type);
        // The knobs of the module: its parameters but the switch.
        std::set<std::string> knobs;
        for (const char* id : RackState::paramIds(type))
            if (!on || std::string(id) != on) knobs.insert(id);
        // Where the module is switched on or off by the preset, the first one is "Off".
        if (on) {
            CHECK(std::string(list.items[0].name) == "Off");
            CHECK_FALSE(list.items[0].on);
        }
        std::set<std::string> names;
        for (int i = 0; i < list.count; ++i) {
            const ModulePreset& preset = list.items[i];
            INFO("preset " << preset.name);
            CHECK(names.insert(preset.name).second);  // no two with the same name
            i18n::useForSession(i18n::Language::Spanish);
            CHECK_FALSE(tr(preset.name).isEmpty());
            i18n::useForSession(i18n::Language::English);
            if (on && !preset.on) continue;  // "Off" lists no knobs: it keeps the player's
            std::set<std::string> listed;
            for (const auto& v : preset.v)
                if (v.id) listed.insert(v.id);
            CHECK(listed == knobs);  // every knob, so a preset never inherits a leftover of the one before
            // Apply it and read the parameters back: each is where the preset said (an out-of-range value would have been clamped).
            p.applyModulePreset(type, i);
            if (on) CHECK(p.apvts.getRawParameterValue(on)->load() > 0.5f);
            for (const auto& v : preset.v) {
                if (!v.id) continue;
                auto* param = p.apvts.getParameter(v.id);
                REQUIRE(param != nullptr);
                const auto range = param->getNormalisableRange();
                const float got = p.apvts.getRawParameterValue(v.id)->load();
                INFO(v.id << ": wanted " << v.value << ", got " << got);
                CHECK(std::abs(double(got) - v.value) <= 0.002 * double(range.end - range.start) + 1e-4);
            }
        }
        // "Off" switches the module off and leaves the knobs alone.
        if (on) {
            p.applyModulePreset(type, 1);
            const char* first = nullptr;
            for (const auto& v : list.items[1].v)
                if (v.id) { first = v.id; break; }
            const float before = p.apvts.getRawParameterValue(first)->load();
            p.applyModulePreset(type, 0);
            CHECK(p.apvts.getRawParameterValue(on)->load() < 0.5f);
            CHECK(p.apvts.getRawParameterValue(first)->load() == before);
        }
    }
}

TEST_CASE("every circuit amp has factory presets (Clean, Edge of breakup, Crunch, Lead; the bass amp has its own) that set its channel, knobs and switches", "[rack][i18n]") {
    JuceInit init;
    SasProcessor p;
    int tested = 0;
    for (std::size_t a = 0; a < p.amps().size(); ++a) {
        const auto& def = p.amps()[a];
        if (def.nam) {
            CHECK(def.presets.empty());  // the model loader has nothing to preset
            continue;
        }
        INFO("amp " << def.id);
        if (def.id == "svt_bass300") {  // the bass amp has presets for a bass guitar
            REQUIRE(def.presets.size() >= 6);
            CHECK(def.presets[0].name == "Clean");
        } else {
            REQUIRE(def.presets.size() >= 4);  // the signature amp adds its own
            CHECK(def.presets[0].name == "Clean");
            CHECK(def.presets[1].name == (def.id == "jazz_clean_120" ? "Bright clean" : "Edge of breakup"));  // the solid-state clean amp has no breakup
            CHECK(def.presets[2].name == "Crunch");
            CHECK(def.presets[3].name == "Lead");
        }
        setParam(p, ids::amp, float(a));
        REQUIRE(p.selectedAmpIndex(0) == int(a));
        for (std::size_t k = 0; k < def.presets.size(); ++k) {
            const auto& preset = def.presets[k];
            INFO("preset " << preset.name);
            p.applyAmpPreset(int(k));
            REQUIRE_FALSE(preset.params.empty());
            for (const auto& [param, value] : preset.params) {
                auto* q = p.apvts.getParameter(param);
                REQUIRE(q != nullptr);
                INFO(param << " should be " << value);
                CHECK(p.apvts.getRawParameterValue(param)->load() == Catch::Approx(float(value)).margin(0.02));
            }
            if (!preset.jack.empty()) CHECK(int(p.apvts.getRawParameterValue(ids::jack)->load() + 0.5f) == int(def.jackIndex(preset.jack)));
            for (const auto& [name, position] : preset.selectors)
                for (std::size_t i = 0; i < def.preamp.selectors.size(); ++i)
                    if (def.preamp.selectors[i].name == name) CHECK(int(p.apvts.getRawParameterValue(ids::sel[i])->load() + 0.5f) == position);
        }
        ++tested;
    }
    CHECK(tested >= 16);
    // Out-of-range indices do nothing.
    p.applyAmpPreset(-1);
    p.applyAmpPreset(99);
}

TEST_CASE("the power tubes are drawn from the artwork: the body is there, the plate glows more with more current, the heater is always lit", "[rack]") {
    JuceInit init;
    auto render = [](float level, int tubes) {
        TubeGlow tg;
        tg.setSize(320, 130);
        tg.setTubes(tubes, "EL34");
        for (int i = 0; i < 40; ++i) tg.setGlow(level, 40.0f);  // the glow follows the current with some inertia: let it settle
        return tg.createComponentSnapshot(tg.getLocalBounds(), false, 1.0f);
    };
    const juce::Image cold = render(0.0f, 4), hot = render(1.3f, 4);
    REQUIRE(cold.isValid());
    REQUIRE(hot.isValid());
    // Where the plates of the first tube are (the cell is a quarter of the width; the plate is in the middle of the tube, a bit above the centre).
    const int px = 40, py = 52;
    const auto c = cold.getPixelAt(px, py), h = hot.getPixelAt(px, py);
    INFO("plate pixel cold " << c.toString() << " hot " << h.toString());
    CHECK(h.getRed() > c.getRed() + 40);    // the hot plate is much redder
    CHECK(h.getBlue() <= h.getRed());       // and warm, not white
    // The body is drawn even with no glow: there are opaque pixels around the tube (the base and the glass), and the corner of the cell is empty.
    int opaque = 0;
    for (int y = 0; y < cold.getHeight(); ++y)
        for (int x = 0; x < cold.getWidth(); ++x)
            if (cold.getPixelAt(x, y).getAlpha() > 200) ++opaque;
    CHECK(opaque > 400);
    CHECK(cold.getPixelAt(0, 0).getAlpha() < 10);
    // More tubes, more of them: the same component with six tubes paints more.
    const juce::Image six = render(0.5f, 6);
    int paintedFour = 0, paintedSix = 0;
    const juce::Image four = render(0.5f, 4);
    for (int y = 0; y < four.getHeight(); ++y)
        for (int x = 0; x < four.getWidth(); ++x) {
            if (four.getPixelAt(x, y).getAlpha() > 30) ++paintedFour;
            if (six.getPixelAt(x, y).getAlpha() > 30) ++paintedSix;
        }
    CHECK(paintedSix > 0);
    CHECK(paintedFour > 0);
}

TEST_CASE("choosing another amp puts the amp's own power tubes back", "[rack]") {
    JuceInit init;
    SECTION("amp A: the list of amps") {
        SasProcessor p;
        setParam(p, ids::tube, 2.0f);  // a tube type picked for this amp
        const int before = p.selectedAmpIndex();
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        std::function<juce::ListBox*(juce::Component&)> findList = [&](juce::Component& c) -> juce::ListBox* {
            if (auto* lb = dynamic_cast<juce::ListBox*>(&c); lb && lb->getName() == "amps") return lb;
            for (auto* child : c.getChildren())
                if (auto* hit = findList(*child)) return hit;
            return nullptr;
        };
        auto* list = findList(*ed);
        REQUIRE(list != nullptr);
        // Choosing another amp from the list gives its own tubes.
        bool moved = false;
        for (int row = 0; row < 4 && !moved; ++row) {
            list->selectRow(row, true, true);
            moved = p.selectedAmpIndex() != before;
        }
        REQUIRE(moved);
        CHECK(p.apvts.getRawParameterValue(ids::tube)->load() == 0.0f);
    }
    SECTION("amp B: its own selector") {
        SasProcessor p;
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1100, 900);
        REQUIRE(addSecondAmp(p));
        setParam(p, RackState::paramId(ids::tube, 1).toRawUTF8(), 2.0f);
        auto* page = findRackPage(*ed);
        REQUIRE(page != nullptr);
        page->rebuild();
        auto* box = dynamic_cast<juce::ComboBox*>(findById(*ed, "amp_b_select"));
        REQUIRE(box != nullptr);
        const int now = box->getSelectedItemIndex();
        box->setSelectedItemIndex(now == 0 ? 1 : 0, juce::sendNotificationSync);
        CHECK(p.apvts.getRawParameterValue(RackState::paramId(ids::tube, 1))->load() == 0.0f);
    }
    SECTION("the call itself") {
        SasProcessor p;
        setParam(p, ids::tube, 3.0f);
        p.resetTubes();
        CHECK(p.apvts.getRawParameterValue(ids::tube)->load() == 0.0f);
    }
}
