#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

#include "aes/amp_def.h"
#include "aes/chain.h"

namespace ids {
inline constexpr const char* volume = "volume";
inline constexpr const char* bass = "bass";
inline constexpr const char* middle = "middle";
inline constexpr const char* treble = "treble";
inline constexpr const char* presence = "presence";
inline constexpr const char* master = "master";
inline constexpr const char* input = "input";
inline constexpr const char* output = "output";
inline constexpr const char* quality = "quality";
inline constexpr const char* cab = "cab";
inline constexpr const char* jack = "jack";
inline constexpr const char* tsOn = "ts_on";
inline constexpr const char* tsDrive = "ts_drive";
inline constexpr const char* tsTone = "ts_tone";
inline constexpr const char* tsLevel = "ts_level";
}  // namespace ids

// Cabinet source. Order is saved in projects: append only.
enum class CabMode { Off = 0, Filter = 1, Ir = 2 };

class AesProcessor final : public juce::AudioProcessor {
public:
    AesProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AES"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Cabinet impulse response. Both are message-thread calls.
    bool loadImpulseResponse(const juce::File& file);
    void clearImpulseResponse();
    juce::String impulseResponseName() const;

    juce::AudioProcessorValueTreeState apvts;
    const aes::AmpDef& ampDef() const { return def_; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name);
    aes::ChainSettings readSettings(bool irAvailable);

    aes::AmpDef def_;
    std::unique_ptr<aes::Chain> chain_;
    double sampleRate_ = 0.0;
    int latencyReported_ = -1;

    juce::dsp::Convolution convolution_{juce::dsp::Convolution::NonUniform{512}};
    std::atomic<bool> irLoaded_{false};
    bool irWasActive_ = false;
    juce::MemoryBlock irBytes_;
    juce::String irName_;
    mutable juce::CriticalSection irLock_;  // guards irBytes_/irName_ (message thread vs state calls)

    std::vector<float> mono_;
    aes::ChainSettings settings_;
    std::vector<std::atomic<float>*> knobParams_;  // in def_.preamp.knobs order (nullptr = use default)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AesProcessor)
};
