// Headless tests of the JUCE processor: parameters, audio, state round trip and the IR loader.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.h"

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
juce::AudioBuffer<float> run(AesProcessor& p, int blocks, double hz = 220.0, double amp = 0.05) {
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b) {
        fill(buf, hz, amp, 48000.0, b * 512);
        p.processBlock(buf, midi);
    }
    return buf;
}

void setParam(AesProcessor& p, const char* id, float plainValue) {
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
    AesProcessor p;
    for (const char* id : {ids::amp, ids::volume2, ids::volume, ids::bass, ids::middle, ids::treble, ids::presence, ids::master, ids::input, ids::output,
                           ids::quality, ids::cab, ids::jack, ids::tsOn, ids::tsDrive, ids::tsTone, ids::tsLevel})
        CHECK(p.apvts.getParameter(id) != nullptr);
    CHECK(p.apvts.getParameter(ids::volume)->getText(p.apvts.getParameter(ids::volume)->getDefaultValue(), 10) == "6.0");
    auto* quality = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(ids::quality));
    REQUIRE(quality != nullptr);
    CHECK(quality->choices[2].contains("Lite"));  // Lite stays last for saved-project compatibility
}

TEST_CASE("processor makes finite audio in every quality and reports latency") {
    JuceInit init;
    AesProcessor p;
    p.prepareToPlay(48000.0, 512);
    for (int q = 0; q < 3; ++q) {
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
    AesProcessor p;
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

TEST_CASE("state round trip restores parameters") {
    JuceInit init;
    AesProcessor a, b;
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
    AesProcessor p;
    p.prepareToPlay(48000.0, 512);
    CHECK(p.impulseResponseName().isEmpty());
    CHECK(!p.loadImpulseResponse(juce::File("C:/definitely/not/here.wav")));

    const auto file = writeImpulse("aes_test_ir.wav");
    REQUIRE(p.loadImpulseResponse(file));
    CHECK(p.impulseResponseName() == "aes_test_ir.wav");

    setParam(p, ids::cab, 2.0f);  // IR
    // The convolution builds its engine in the background: give it a moment, then audio must flow.
    juce::Thread::sleep(300);
    const auto withIr = run(p, 40);
    for (int i = 0; i < withIr.getNumSamples(); ++i) REQUIRE(std::isfinite(withIr.getSample(0, i)));
    CHECK(rms(withIr) > 1e-5);

    juce::MemoryBlock state;
    p.getStateInformation(state);
    AesProcessor q;
    q.setStateInformation(state.getData(), int(state.getSize()));
    CHECK(q.impulseResponseName() == "aes_test_ir.wav");  // the IR travels inside the project

    p.clearImpulseResponse();
    CHECK(p.impulseResponseName().isEmpty());
    file.deleteFile();
}
