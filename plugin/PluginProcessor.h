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
#include "sas/delay.h"
#include "sas/eq.h"
#include "sas/gate.h"
#include "sas/pitch_shifter.h"
#include "sas/plate.h"
#include "sas/stereo.h"
#include "sas/tuner.h"

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
inline constexpr const char* sel[4] = {"sel1", "sel2", "sel3", "sel4"};  // the amp's own switches (amp.json "selectors"), 5 positions at most
inline constexpr const char* tsOn = "ts_on";
inline constexpr const char* tsDrive = "ts_drive";
inline constexpr const char* tsTone = "ts_tone";
inline constexpr const char* tsLevel = "ts_level";
inline constexpr const char* pickup = "pickup";  // guitar impedance: Off / single coil / humbucker / P-90
inline constexpr const char* cable = "cable";    // cable length in metres
inline constexpr const char* eqOn = "eq_on";  // the post equaliser: five bands, a high-pass and a low-pass
inline constexpr const char* eqHpf = "eq_hpf";
inline constexpr const char* eqLpf = "eq_lpf";
inline constexpr const char* eqFreq[5] = {"eq1_freq", "eq2_freq", "eq3_freq", "eq4_freq", "eq5_freq"};
inline constexpr const char* eqGain[5] = {"eq1_gain", "eq2_gain", "eq3_gain", "eq4_gain", "eq5_gain"};
inline constexpr const char* eqQ[5] = {"eq1_q", "eq2_q", "eq3_q", "eq4_q", "eq5_q"};
inline constexpr const char* pitch = "pitch";            // pitch shift in semitones (0 = off)
inline constexpr const char* pitchFine = "pitch_fine";   // and in cents
inline constexpr const char* gateOn = "gate_on";
inline constexpr const char* gateThreshold = "gate_threshold";
inline constexpr const char* gateRelease = "gate_release";
inline constexpr const char* bias = "bias";    // output-tube bias, 5 = as drawn (amps with a bias supply)
inline constexpr const char* tube = "tube";    // output tube type: 0 = the amp's own, else one of tubeTypes (the amp's list may differ in order)
inline constexpr const char* tubeTypes[] = {"Stock", "EL34", "6L6GC", "6550"};
inline constexpr const char* noise = "noise";  // hiss and hum: Off / Low / Real
inline constexpr const char* wear = "wear";    // 0..10 tenths of wear
inline constexpr const char* unit = "unit";    // 0 = ideal parts, 1..9 = an individual unit
inline constexpr const char* compOn = "comp_on";            // compressor pedal
inline constexpr const char* compSustain = "comp_sustain";
inline constexpr const char* compBlend = "comp_blend";
inline constexpr const char* compTone = "comp_tone";
inline constexpr const char* compLevel = "comp_level";
inline constexpr const char* compFast = "comp_fast";        // the pedal's Attack switch: fast release
inline constexpr const char* tunerOn = "tuner_on";        // the chromatic tuner listens to the input
inline constexpr const char* tunerMute = "tuner_mute";    // and silences the output while it is on
inline constexpr const char* tunerRef = "tuner_ref";      // reference pitch of A4, 430..450 Hz
inline constexpr const char* irWidth = "ir_width";        // 0 = A and B blended in both ears, 10 = A on the left and B on the right
inline constexpr const char* doubleAmount = "double_amount";  // automatic double tracking: how much of the second take
inline constexpr const char* doubleTime = "double_time";      // its delay, 8..30 ms
inline constexpr const char* irBlend = "ir_blend";  // 0 = impulse response A only, 10 = B only
inline constexpr const char* speaker = "speaker";               // speaker dynamics amount, 0..10
inline constexpr const char* speakerIntense = "speaker_intense";  // stronger speaker dynamics
// Delay and reverb at the end of the chain (see sas/delay.h and sas/plate.h).
inline constexpr const char* dlyOn = "dly_on";
inline constexpr const char* dlyTime = "dly_time";      // ms, used when dlySync is Free
inline constexpr const char* dlySync = "dly_sync";      // Free / 1/4 / dotted 1/8 / 1/8 / 1/16 of the host tempo
inline constexpr const char* dlyFeedback = "dly_fb";    // percent
inline constexpr const char* dlyMix = "dly_mix";        // percent
inline constexpr const char* dlyTone = "dly_tone";      // Hz, low-pass in the repeats
inline constexpr const char* dlyPing = "dly_ping";
inline constexpr const char* dlyAnalog = "dly_analog";
inline constexpr const char* plOn = "pl_on";
inline constexpr const char* plHall = "pl_hall";        // plate (off) or hall (on)
inline constexpr const char* plDecay = "pl_decay";      // seconds
inline constexpr const char* plPre = "pl_pre";          // ms
inline constexpr const char* plTone = "pl_tone";        // Hz, high-cut in the tail
inline constexpr const char* plLow = "pl_low";          // Hz, high-pass on the wet signal
inline constexpr const char* plMix = "pl_mix";          // percent
inline constexpr const char* plMod = "pl_mod";          // percent
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
    // Two slots (A = 0, B = 1) that are mixed with the "ir_blend" parameter; with only one loaded it plays alone.
    static constexpr int kIrSlots = 2;
    bool loadImpulseResponse(const juce::File& file, int slot = 0);
    // The impulse responses carried inside the plugin (plugin/cabs): how many, their display names, and loading one into a slot.
    static int builtInImpulseResponseCount();
    static juce::String builtInImpulseResponseName(int index);
    bool loadBuiltInImpulseResponse(int index, int slot = 0);
    // Presets: the whole state (amp, knobs, effects, EQ, pitch, impulse responses) as a text file, so a standalone session can keep its settings.
    static juce::File presetFolder();
    bool savePreset(const juce::File& file);
    bool loadPreset(const juce::File& file);
    // One of the factory presets of the equaliser (EqPresets.h; 0 = flat): sets only the EQ parameters and switches the EQ on.
    void applyEqPreset(int index);
    void applyDelayPreset(int index);   // each effect has its own presets (plugin/FxPresets.h)
    void applyReverbPreset(int index);
    void clearImpulseResponse(int slot = 0);
    juce::String impulseResponseName(int slot = 0) const;
    // Magnitude response of the loaded IR in dB (mean 0), log-spaced from 50 Hz to 10 kHz; empty when nothing is loaded.
    std::vector<float> impulseResponseCurve(int slot = 0) const;

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
    // The decimated input for the tuner (filled while the tuner is on); the editor runs the pitch detection on it.
    const sas::TunerFeed& tunerFeed() const { return tuner_; }
    // Average plate current of one output tube in mA (0 in the Lite / Eco modes).
    float idleMilliamps() const { return idleMa_.load(); }
    // The physical condition (tube, wear, unit) the "tube", "wear" and "unit" parameters ask for.
    sas::Condition wantedCondition() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    class ChainBuilder;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout(const std::vector<sas::AmpDef>& defs);
    bool loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name, int slot);
    bool anyIrLoaded() const { return ir_[0].loaded.load() || ir_[1].loaded.load(); }
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

    struct IrSlot {
        juce::dsp::Convolution conv{juce::dsp::Convolution::NonUniform{512}};
        std::atomic<bool> loaded{false};
        juce::MemoryBlock bytes;      // the file as loaded, kept for the project state
        juce::String name;
        std::vector<float> curve;     // magnitude response for the editor
    };
    IrSlot ir_[kIrSlots];
    bool irWasActive_ = false;
    float blendGainA_ = 1.0f, blendGainB_ = 0.0f;  // the gains in force at the end of the last block (ramped to the new ones)
    std::vector<float> mono2_, monoR_;  // the second IR's signal, and the right channel
    float widthNow_ = 0.0f;             // the A / B width in force at the end of the last block
    sas::StereoDouble double_;
    mutable juce::CriticalSection irLock_;  // guards the slots' bytes / name / curve (message thread vs state calls)

    sas::NoiseGate gate_;
    sas::PitchShifter pitch_;
    sas::ParametricEq eq_;
    sas::StereoDelay delay_;
    sas::PlateReverb plate_;
    bool pitchWasActive_ = false;
    sas::TunerFeed tuner_;
    std::atomic<float> inputPeak_{0.0f}, outputPeak_{0.0f}, gateGain_{1.0f}, idleMa_{0.0f};
    std::vector<float> mono_;
    sas::ChainSettings settings_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasProcessor)
};
