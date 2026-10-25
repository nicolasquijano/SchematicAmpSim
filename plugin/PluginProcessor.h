#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <vector>

#include "sas/amp_def.h"
#include "sas/chain.h"

namespace ids {
inline constexpr const char* amp = "amp";
// Knob slots: each amp's amp.json "ui" block says which slot drives which of its knobs.
inline constexpr const char* volume = "volume";
inline constexpr const char* volume2 = "volume2";
inline constexpr const char* bass = "bass";
inline constexpr const char* middle = "middle";
inline constexpr const char* treble = "treble";
inline constexpr const char* presence = "presence";
inline constexpr const char* master = "master";
inline constexpr const char* power = "power";
inline constexpr const char* vibSpeed = "vib_speed";          // optical tremolo (amps that have one)
inline constexpr const char* vibIntensity = "vib_intensity";
inline constexpr const char* vibOn = "vib_on";
inline constexpr const char* input = "input";
inline constexpr const char* output = "output";
inline constexpr const char* quality = "quality";
inline constexpr const char* cab = "cab";
inline constexpr const char* jack = "jack";
inline constexpr const char* levelMatch = "level_match";
inline constexpr const char* tsOn = "ts_on";
inline constexpr const char* tsDrive = "ts_drive";
inline constexpr const char* tsTone = "ts_tone";
inline constexpr const char* tsLevel = "ts_level";
}  // namespace ids

// Cabinet source. Order is saved in projects: append only.
enum class CabMode { Off = 0, Filter = 1, Ir = 2 };

class SasProcessor final : public juce::AudioProcessor {
public:
    SasProcessor();
    ~SasProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Schematic Amp Sim"; }
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

    // Amps available in this build (sorted by id) and the one the "amp" parameter selects.
    const std::vector<sas::AmpDef>& amps() const { return defs_; }
    int selectedAmpIndex() const;
    // The amp whose circuits are processing audio right now (differs from the selection while a
    // new amp is being built).
    int runningAmpIndex() const { return activeAmp_.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    class ChainBuilder;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout(const std::vector<sas::AmpDef>& defs);
    void loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name);
    sas::ChainSettings readSettings(bool irAvailable);
    void swapInBuiltChain();

    std::vector<sas::AmpDef> defs_;

    // Per amp: the APVTS value behind each preamp knob (nullptr = the knob's default) and presence.
    struct AmpParams {
        std::vector<std::atomic<float>*> knobs;
        std::vector<std::atomic<float>*> powerKnobs;
        std::atomic<float>* presence = nullptr;
        std::atomic<float>* vibSpeed = nullptr;
    };
    std::vector<AmpParams> ampParams_;

    std::unique_ptr<sas::Chain> chain_;
    std::atomic<int> activeAmp_{0};  // the amp chain_ was built for (written by the audio thread)
    double sampleRate_ = 0.0;
    int latencyReported_ = -1;

    // A chain for another amp is built off the audio thread and swapped in between blocks.
    std::unique_ptr<ChainBuilder> builder_;
    std::atomic<sas::Chain*> ready_{nullptr};
    std::atomic<int> readyAmp_{-1};
    std::atomic<sas::Chain*> retired_{nullptr};
    std::atomic<int> requestedAmp_{-1};

    juce::dsp::Convolution convolution_{juce::dsp::Convolution::NonUniform{512}};
    std::atomic<bool> irLoaded_{false};
    bool irWasActive_ = false;
    juce::MemoryBlock irBytes_;
    juce::String irName_;
    mutable juce::CriticalSection irLock_;  // guards irBytes_/irName_ (message thread vs state calls)

    std::vector<float> mono_;
    sas::ChainSettings settings_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasProcessor)
};
