// Headless tests of the JUCE processor: parameters, audio, state round trip and the IR loader.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdlib>

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginEditor.h"
#include "EqPresets.h"

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

TEST_CASE("an impulse response is applied for any IR rate, host rate, load order, re-preparation and block size") {
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
        const char* ids4[4] = {"twin_65", "tweed_5e3", "highgain_120", "ac30_normal"};
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

    // The Effects page with the pedals and the gate switched on.
    {
        SasProcessor p;
        setParam(p, ids::compOn, 1.0f);
        setParam(p, ids::tsOn, 1.0f);
        setParam(p, ids::gateOn, 1.0f);
        setParam(p, ids::noise, 2.0f);
        setParam(p, ids::wear, 3.0f);
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1000, 520);
        play(p, *dynamic_cast<SasEditor*>(ed.get()), 196.0, 0.2);
        clickTab(*ed, "Effects");
        savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, scale), "effects.png");
    }

    // The Cabinet page with two impulse responses, a little width and double tracking.
    {
        auto makeIr = [](const juce::String& name, float smoothing, float decay) {
            auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
            file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 24, {}, 0));
            juce::AudioBuffer<float> ir(1, 12000);
            juce::Random rnd(11);
            float lp = 0.0f;
            for (int i = 0; i < ir.getNumSamples(); ++i) {
                const float noise = rnd.nextFloat() * 2.0f - 1.0f;
                lp += smoothing * (noise - lp);
                ir.setSample(0, i, (smoothing < 0.5f ? lp : noise - 0.6f * lp) * std::exp(-float(i) / decay));
            }
            w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
            return file;
        };
        const auto a = makeIr("Cabinet A.wav", 0.12f, 700.0f), b = makeIr("Cabinet B.wav", 0.5f, 500.0f);
        SasProcessor p;
        setParam(p, ids::cab, 2.0f);
        p.prepareToPlay(48000.0, 512);
        REQUIRE(p.loadImpulseResponse(a, 0));
        REQUIRE(p.loadImpulseResponse(b, 1));
        setParam(p, ids::irWidth, 4.0f);
        setParam(p, ids::doubleAmount, 3.0f);
        setParam(p, ids::speaker, 3.0f);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        ed->setSize(1000, 520);
        auto* se = dynamic_cast<SasEditor*>(ed.get());
        play(p, *se, 196.0, 0.2);
        se->updateImpulseStatus();
        clickTab(*ed, "Cabinet");
        savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, scale), "cabinet.png");
        a.deleteFile();
        b.deleteFile();
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
}

TEST_CASE("the editor builds for every amp and can be rendered") {
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
                if (auto* se2 = dynamic_cast<SasEditor*>(ed.get())) for (int i = 0; i < 24; ++i) { run(p, 1, 220.0, 0.2); se2->updateSpectrum(); }
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
        for (int b = 0; b < 5; ++b) {
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
    for (int i = 0; i < 1200 && !swapped; ++i) { run(p, 2, 3000.0, 0.00002); swapped = p.runningAmpIndex() == svt; juce::Thread::sleep(15); }
    REQUIRE(swapped);
    run(p, 20, 3000.0, 0.00002);
    const double off = rms(run(p, 10, 3000.0, 0.00002));
    setParam(p, ids::sel[1], 1.0f);  // Ultra Hi
    run(p, 20, 3000.0, 0.00002);
    const double on = rms(run(p, 10, 3000.0, 0.00002));
    INFO("ultra hi off " << off << ", on " << on);
    CHECK(on > 1.2 * off);
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
