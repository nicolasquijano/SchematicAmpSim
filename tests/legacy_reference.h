#pragma once
// Reference renders of the processor with the fixed-order chain, kept as golden files so the modular rack can be proven to sound the same
// (docs/MODULAR_RACK.md, section 4.3). Cases are named; each one configures a fresh processor through its parameters.
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "PluginProcessor.h"

#ifndef SAS_GOLDEN_DIR
#error "SAS_GOLDEN_DIR must point at the golden/ folder"
#endif

namespace legacy {

inline void set(SasProcessor& p, const char* id, float plain) {
    auto* q = p.apvts.getParameter(id);
    q->setValueNotifyingHost(q->convertTo0to1(plain));
}

inline juce::File impulseFile() {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("sas_legacy_ir.wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(file), 48000.0, 1, 24, {}, 0));
    juce::AudioBuffer<float> ir(1, 256);
    ir.clear();
    ir.setSample(0, 0, 1.0f);
    ir.setSample(0, 40, 0.4f);
    ir.setSample(0, 120, -0.2f);
    w->writeFromAudioSampleBuffer(ir, 0, ir.getNumSamples());
    return file;
}

struct Case {
    const char* name;
    std::function<void(SasProcessor&)> setup;
};

inline std::vector<Case> rawCases() {
    using ids::amp;
    return {
        {"default", [](SasProcessor&) {}},
        {"pre_mods", [](SasProcessor& p) {
             set(p, ids::phOn, 1); set(p, ids::phPost, 0);
             set(p, ids::chOn, 1); set(p, ids::chPost, 0);
             set(p, ids::vbOn, 1); set(p, ids::vbPost, 0);
         }},
        {"post_mods", [](SasProcessor& p) {
             set(p, ids::phOn, 1); set(p, ids::phPost, 1);
             set(p, ids::chOn, 1); set(p, ids::chPost, 1);
             set(p, ids::vbOn, 1); set(p, ids::vbPost, 1);
         }},
        {"comp_ts", [](SasProcessor& p) { set(p, ids::compOn, 1); set(p, ids::tsOn, 1); set(p, ids::tsDrive, 7); }},
        {"efficient_ts", [](SasProcessor& p) { set(p, ids::quality, 4); set(p, ids::tsOn, 1); }},
        {"ir", [](SasProcessor& p) { set(p, ids::cab, 2); p.loadImpulseResponse(impulseFile(), 0); }},
        {"space", [](SasProcessor& p) { set(p, ids::dlyOn, 1); set(p, ids::plOn, 1); }},
        {"eq_pitch_gate", [](SasProcessor& p) {
             set(p, ids::eqGain[2], 6.0f); set(p, ids::eqHpf, 120.0f);
             set(p, ids::pitch, 7.0f);
             set(p, ids::gateOn, 1); set(p, ids::gateThreshold, -50.0f);
         }},
        {"nam_pedal", [](SasProcessor& p) {
             p.loadNamPedal(juce::File(SAS_NAM_EXAMPLES_DIR "/wavenet.nam"));
             set(p, ids::nmpOn, 1);
         }},
    };
}

// The references were rendered when a new instance opened in Normal quality: every case sets it first (a case may then choose another).
inline std::vector<Case> cases() {
    auto list = rawCases();
    for (auto& c : list) {
        auto inner = c.setup;
        c.setup = [inner](SasProcessor& p) {
            set(p, ids::quality, 0);
            inner(p);
        };
    }
    return list;
}

// A fixed, guitar-like test signal: two decaying plucks of a 110 Hz chord, 0.5 s at 48 kHz.
inline std::vector<float> input() {
    std::vector<float> v(24000);
    for (std::size_t i = 0; i < v.size(); ++i) {
        const double t = double(i) / 48000.0, env = std::exp(-3.0 * std::fmod(t, 0.25));
        v[i] = float(0.12 * env * (std::sin(2 * 3.14159265358979 * 110.0 * t) + 0.5 * std::sin(2 * 3.14159265358979 * 165.0 * t)));
    }
    return v;
}

// Plays `in` through the processor in blocks of 256 and returns the left channel.
inline std::vector<float> render(SasProcessor& p, const std::vector<float>& in) {
    p.prepareToPlay(48000.0, 256);
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

inline std::string goldenPath(const std::string& name) { return std::string(SAS_GOLDEN_DIR) + "/rack_legacy_" + name + ".f32"; }

inline std::vector<float> readGolden(const std::string& name) {
    std::ifstream f(goldenPath(name), std::ios::binary | std::ios::ate);
    std::vector<float> v(f ? std::size_t(f.tellg()) / sizeof(float) : 0);
    if (f) {
        f.seekg(0);
        f.read(reinterpret_cast<char*>(v.data()), std::streamsize(v.size() * sizeof(float)));
    }
    return v;
}

inline void writeGolden(const std::string& name, const std::vector<float>& v) {
    std::ofstream f(goldenPath(name), std::ios::binary);
    f.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size() * sizeof(float)));
}



}  // namespace legacy
