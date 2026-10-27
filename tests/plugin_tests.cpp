// Headless tests of the JUCE processor: parameters, audio, state round trip and the IR loader.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdlib>

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginEditor.h"

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
    setParam(p, ids::tube, 1.0f);  // 6L6GC instead of EL34: a bigger tube, so more plate current at the same share of its rating
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
    CHECK(p.impulseResponseName().isEmpty());
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

TEST_CASE("the editor builds for every amp and can be rendered") {
    JuceInit juceInit;
    SasProcessor p;
    const char* dir = std::getenv("SAS_EDITOR_PNG_DIR");  // set it to also write a picture of every amp's editor
    for (int amp = 0; amp < p.apvts.getParameter("amp")->getNumSteps(); ++amp) {
        setParam(p, "amp", float(amp));
        p.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        REQUIRE(ed != nullptr);
        ed->setSize(800, 410);
        run(p, 40, 220.0, 0.2);  // so the meters have something to show
        if (auto* se = dynamic_cast<SasEditor*>(ed.get())) for (int i = 0; i < 12; ++i) { se->pollMeters(); run(p, 2, 220.0, 0.2); }
        CHECK(ed->getWidth() > 0);
        if (dir) {
            const auto img = ed->createComponentSnapshot(ed->getLocalBounds());
            juce::PNGImageFormat png;
            juce::FileOutputStream out(juce::File(dir).getChildFile("amp" + juce::String(amp) + ".png"));
            if (out.openedOk()) { out.setPosition(0); out.truncate(); png.writeImageToStream(img, out); }
            if (amp == 0) {  // the effects page
                for (auto* c : ed->getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText() == "Effects") { b->setToggleState(true, juce::dontSendNotification); b->onClick(); }
                const auto fx = ed->createComponentSnapshot(ed->getLocalBounds());
                juce::FileOutputStream o2(juce::File(dir).getChildFile("fx.png"));
                if (o2.openedOk()) { o2.setPosition(0); o2.truncate(); png.writeImageToStream(fx, o2); }
            }
        }
    }
}
