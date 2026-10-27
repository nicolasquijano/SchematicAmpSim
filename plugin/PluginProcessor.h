#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <vector>

#include "sas/amp_def.h"
#include "sas/chain.h"
#include "sas/condition.h"
#include "sas/gate.h"

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
inline constexpr const char* resonance = "resonance";  // power-amp feedback shaping (amps that have it)
inline constexpr const char* vibSpeed = "vib_speed";          // optical tremolo (amps that have one)
inline constexpr const char* vibIntensity = "vib_intensity";
inline constexpr const char* vibOn = "vib_on";
inline constexpr const char* reverb = "reverb";  // spring reverb level (amps that have one)
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
inline constexpr const char* pickup = "pickup";  // guitar impedance: Off / single coil / humbucker / P-90
inline constexpr const char* cable = "cable";    // cable length in metres
inline constexpr const char* gateOn = "gate_on";
inline constexpr const char* gateThreshold = "gate_threshold";
inline constexpr const char* gateRelease = "gate_release";
inline constexpr const char* bias = "bias";    // output-tube bias, 5 = as drawn (amps with a bias supply)
inline constexpr const char* tube = "tube";    // output tube type (index into the amp's tube list)
inline constexpr const char* noise = "noise";  // hiss and hum: Off / Low / Real
inline constexpr const char* wear = "wear";    // 0..10 tenths of wear
inline constexpr const char* unit = "unit";    // 0 = ideal parts, 1..9 = an individual unit
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

    // Peak level (linear, 0..1+) since the last call, for the editor's meters.
    float takeInputPeak() { return inputPeak_.exchange(0.0f); }
    float takeOutputPeak() { return outputPeak_.exchange(0.0f); }
    float gateGain() const { return gateGain_.load(); }
    // Average plate current of one output tube in mA (0 in the Lite / Eco modes).
    float idleMilliamps() const { return idleMa_.load(); }
    // The physical condition (tube, wear, unit) the "tube", "wear" and "unit" parameters ask for.
    sas::Condition wantedCondition() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    class ChainBuilder;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout(const std::vector<sas::AmpDef>& defs);
    bool loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name);
    sas::ChainSettings readSettings(bool irAvailable);
    void swapInBuiltChain();
    int wantedKey() const;
    static int makeKey(int amp, const sas::Condition& c) { return amp + 32 * (c.tube + 8 * (c.wear + 16 * c.unit)); }

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
    std::atomic<int> readyKey_{-1};
    std::atomic<sas::Chain*> retired_{nullptr};
    std::atomic<int> requestedKey_{-1};
    std::atomic<int> activeKey_{0};  // amp + condition chain_ was built for

    juce::dsp::Convolution convolution_{juce::dsp::Convolution::NonUniform{512}};
    std::atomic<bool> irLoaded_{false};
    bool irWasActive_ = false;
    juce::MemoryBlock irBytes_;
    juce::String irName_;
    mutable juce::CriticalSection irLock_;  // guards irBytes_/irName_ (message thread vs state calls)

    sas::NoiseGate gate_;
    std::atomic<float> inputPeak_{0.0f}, outputPeak_{0.0f}, gateGain_{1.0f}, idleMa_{0.0f};
    std::vector<float> mono_;
    sas::ChainSettings settings_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasProcessor)
};
