#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "RackState.h"
#include "sas/amp_def.h"
#include "sas/chain.h"
#include "sas/chorus.h"
#include "sas/flanger.h"
#include "sas/tremolo.h"
#include "sas/condition.h"
#include "sas/delay.h"
#include "sas/eq.h"
#include "sas/gate.h"
#include "sas/phaser.h"
#include "sas/rack_modules.h"
#include "sas/pitch_shifter.h"
#include "sas/plate.h"
#include "sas/spectrum.h"
#include "sas/stereo.h"
#include "sas/vibe.h"
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
inline constexpr const char* mixLevelA = "mix_level_a";  // the mixer of a parallel: level, pan and phase of each lane
inline constexpr const char* mixLevelB = "mix_level_b";
inline constexpr const char* mixPanA = "mix_pan_a";
inline constexpr const char* mixPanB = "mix_pan_b";
inline constexpr const char* mixInvertB = "mix_invert_b";
inline constexpr const char* cabOn = "cab_on";  // the cabinet module is on (off: the raw amp)
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
inline constexpr const char* nmpOn = "nam_pedal_on";           // a NAM model of a pedal in front of the amp
inline constexpr const char* nmpDrive = "nam_pedal_drive";     // dB into the model
inline constexpr const char* nmpLevel = "nam_pedal_level";     // dB out of the model
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
// The phaser (sas/phaser.h): in front of the amp or after it.
inline constexpr const char* phOn = "ph_on";
inline constexpr const char* phSpeed = "ph_speed";      // Hz
inline constexpr const char* phIntense = "ph_intense";  // with feedback
inline constexpr const char* phStereo = "ph_stereo";    // 0 = mono (both channels sweep together), 1 = stereo (the right one half a cycle apart)
inline constexpr const char* phPost = "ph_post";        // 0 = before the amp, 1 = after the amp (and the cabinet)
// The chorus (sas/chorus.h): two modes, before the amp or after it.
inline constexpr const char* chOn = "ch_on";
inline constexpr const char* chMode = "ch_mode";    // 0 = CE-2 (one voice), 1 = Juno (two voices)
inline constexpr const char* chRate = "ch_rate";    // Hz, CE-2
inline constexpr const char* chDepth = "ch_depth";  // percent, CE-2
inline constexpr const char* chJuno = "ch_juno";    // Juno button: I, II, I+II
inline constexpr const char* chStereo = "ch_stereo";  // 0 = mono, 1 = stereo (CE-2: the right channel sweeps the other way)
inline constexpr const char* chPost = "ch_post";    // 0 = before the amp, 1 = after the amp (and the cabinet)
// The vibe (sas/vibe.h): before the amp or after it.
inline constexpr const char* vbOn = "vb_on";
inline constexpr const char* vbSpeed = "vb_speed";          // Hz
inline constexpr const char* vbIntensity = "vb_intensity";  // percent
inline constexpr const char* vbVibrato = "vb_vibrato";      // false: chorus mode, true: vibrato mode
inline constexpr const char* vbStereo = "vb_stereo";          // 0 = mono, 1 = stereo (the right lamp half a cycle apart)
inline constexpr const char* vbPost = "vb_post";            // 0 = before the amp, 1 = after the amp (and the cabinet)
// The tremolo and the flanger (sas/tremolo.h, sas/flanger.h): modules of the rack only, wherever the player puts them.
inline constexpr const char* trOn = "tr_on";
inline constexpr const char* trRate = "tr_rate";      // Hz
inline constexpr const char* trDepth = "tr_depth";    // percent
inline constexpr const char* trShape = "tr_shape";    // sine / triangle / square
inline constexpr const char* trStereo = "tr_stereo";  // auto-pan on a stereo pair
inline constexpr const char* flOn = "fl_on";
inline constexpr const char* flRate = "fl_rate";          // Hz
inline constexpr const char* flDepth = "fl_depth";        // percent
inline constexpr const char* flManual = "fl_manual";      // percent, centre of the sweep
inline constexpr const char* flFeedback = "fl_feedback";  // percent, -90 .. 90
inline constexpr const char* flStereo = "fl_stereo";      // 0 = mono, 1 = stereo (the right channel a quarter of a cycle behind)
// Delay and reverb at the end of the chain (see sas/delay.h and sas/plate.h).
inline constexpr const char* dlyOn = "dly_on";
inline constexpr const char* dlyTime = "dly_time";      // ms, used when dlySync is Free
inline constexpr const char* dlySync = "dly_sync";      // Free / 1/4 / dotted 1/8 / 1/8 / 1/16 of the host tempo
inline constexpr const char* dlyFeedback = "dly_fb";    // percent
inline constexpr const char* dlyMix = "dly_mix";        // percent
inline constexpr const char* dlyTone = "dly_tone";      // Hz, low-pass in the repeats
inline constexpr const char* dlyPing = "dly_ping";
inline constexpr const char* dlyAnalog = "dly_analog";
inline constexpr const char* dlyStereo = "dly_stereo";  // 0 = mono (as it always was), 1 = stereo (the two ears hear different echoes)
inline constexpr const char* plOn = "pl_on";
inline constexpr const char* plHall = "pl_hall";        // plate (off) or hall (on)
inline constexpr const char* plDecay = "pl_decay";      // seconds
inline constexpr const char* plPre = "pl_pre";          // ms
inline constexpr const char* plTone = "pl_tone";        // Hz, high-cut in the tail
inline constexpr const char* plLow = "pl_low";          // Hz, high-pass on the wet signal
inline constexpr const char* plMix = "pl_mix";          // percent
inline constexpr const char* plMod = "pl_mod";          // percent
inline constexpr const char* plShimmer = "pl_shim";    // percent: the shifted tail fed back (0 = off)
inline constexpr const char* plStereo = "pl_stereo";  // 0 = mono (the same tail on both sides), 1 = stereo (as it always was)
inline constexpr const char* plShimmerInt = "pl_shimint";  // 0 = octave, 1 = octave and a fifth
}  // namespace ids

// Cabinet source. Order is saved in projects: append only.
enum class CabMode { Off = 0, Filter = 1, Ir = 2, Nam = 3 };

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
    // Every impulse call takes the cabinet too (0, or 1 for the second cabinet of a parallel).
    bool loadImpulseResponse(const juce::File& file, int slot = 0, int cabinet = 0);
    // The impulse responses carried inside the plugin (plugin/cabs): how many, their display names, and loading one into a slot.
    static int builtInImpulseResponseCount();
    static juce::String builtInImpulseResponseName(int index);
    bool loadBuiltInImpulseResponse(int index, int slot = 0, int cabinet = 0);
    // The Neural Amp Modeler loader amp ("NAM Loader"): the .nam model it runs. Message-thread calls. The model's bytes are kept in the project
    // state, so a project opens with its model on another machine. A model that fails to load leaves the previous one in place.
    bool loadNamModel(const juce::File& file);
    void clearNamModel();
    bool namModelLoaded() const { return namActive_.load() != nullptr; }
    juce::String namModelName() const;
    // Why the last load failed, or a warning about the loaded model (trained at another sample rate); empty when there is nothing to say.
    juce::String namModelMessage() const;
    // A Neural Amp Modeler model used as the cabinet ("NAM" in the Cabinet selector): captures of a speaker cabinet and microphone, run after
    // the amp and the speaker in place of the impulse responses. Same rules as the amp's model: message-thread calls, kept in the project state.
    bool loadNamCab(const juce::File& file);
    void clearNamCab();
    bool namCabLoaded() const { return namCabActive_.load() != nullptr; }
    juce::String namCabName() const;
    juce::String namCabMessage() const;
    // A Neural Amp Modeler model of a pedal (overdrive, boost, fuzz...) in front of the amp, behind the other pedals of the Mod page. Same rules as the amp's
    // model: message-thread calls, kept in the project state; it costs nothing while the switch is off.
    bool loadNamPedal(const juce::File& file, int instance = 0);
    void clearNamPedal(int instance = 0);
    bool namPedalLoaded(int instance = 0) const { return namPedals_[instance].active.load() != nullptr; }
    juce::String namPedalName(int instance = 0) const;
    juce::String namPedalMessage(int instance = 0) const;
    // Presets: the whole state (amp, knobs, effects, EQ, pitch, impulse responses) as a text file, so a standalone session can keep its settings.
    static juce::File presetFolder();
    bool savePreset(const juce::File& file);
    bool loadPreset(const juce::File& file);
    // One of the factory presets of the equaliser (EqPresets.h; 0 = flat): sets only the EQ parameters and switches the EQ on.
    void applyEqPreset(int index, int instance = 0);
    const sas::SpectrumFeed& spectrumFeed() const { return spectrumFeed_; }
    void applyDelayPreset(int index, int instance = 0);   // each effect has its own presets (plugin/FxPresets.h)
    void applyReverbPreset(int index, int instance = 0);
    void clearImpulseResponse(int slot = 0, int cabinet = 0);
    juce::String impulseResponseName(int slot = 0, int cabinet = 0) const;
    // Magnitude response of the loaded IR in dB (mean 0), log-spaced from 50 Hz to 10 kHz; empty when nothing is loaded.
    std::vector<float> impulseResponseCurve(int slot = 0, int cabinet = 0) const;

    // Amps available in this build (sorted by id) and the one the "amp" parameter selects.
    const std::vector<sas::AmpDef>& amps() const { return defs_; }
    int selectedAmpIndex(int instance = 0) const;
    // The amp whose circuits are processing audio right now (differs from the selection while a
    // new amp is being built).
    int runningAmpIndex(int instance = 0) const { return amps_[instance].activeAmp.load(); }

    // Peak level (linear, 0..1+) since the last call, for the editor's meters.
    float takeInputPeak() { return inputPeak_.exchange(0.0f); }
    float takeOutputPeak() { return outputPeak_.exchange(0.0f); }
    float gateGain() const { return gateGain_.load(); }
    // The decimated input for the tuner (filled while the tuner is on); the editor runs the pitch detection on it.
    const sas::TunerFeed& tunerFeed() const { return tuner_; }
    // Average plate current of one output tube in mA (0 in the Lite / Eco modes).
    float idleMilliamps() const { return idleMa_.load(); }
    // The physical condition (tube, wear, unit) the "tube", "wear" and "unit" parameters ask for.
    sas::Condition wantedCondition(int instance = 0) const;

    // The rack: the order of the modules (docs/MODULAR_RACK.md). A new instance starts with the amp and the cabinet.
    const sas::RackOrder& rackOrder() const { return rackOrder_; }
    const sas::Rack& rack() const { return rack_; }
    void setRackOrder(const sas::RackOrder& order);
    // Message thread. They build or retire the module, move it and publish the new order (the output ducks for a moment so nothing clicks).
    // addModule puts a new module at `insertAt` (-1: where it usually goes: pedals in front of the amp, the cabinet behind it, the time effects last)
    // and returns false when the type is at its limit or the rack is full.
    // `lane` 1 puts it in lane B (needs a parallel); -1 lets it join the lane of the module before it.
    bool addModule(sas::ModuleType type, int insertAt = -1, int lane = -1);
    bool canAdd(sas::ModuleType type) const;
    bool removeModule(sas::SlotRef slot);
    bool moveModule(sas::SlotRef slot, int newIndex);
    // The parallel (docs/superpowers/specs/2026-11-03-parallel-paths-design.md): a splitter and a mixer with a second amp and cabinet in lane B.
    // addParallel puts the splitter in front of the amp (and the compressor and overdrive that sit next to it), lane B behind the cabinet, and the
    // mixer after that; removeParallel takes the two ends, lane B and everything in it away (lane A stays where it was, in series).
    bool hasParallel() const { return rackOrder_.splitterIndex() >= 0; }
    bool canAddParallel() const;
    bool addParallel();
    bool removeParallel();
    // Moves a module to a lane (0: A, 1: B) at a position of the order; only between the splitter and the mixer.
    bool moveModuleToLane(sas::SlotRef slot, int lane, int newIndex);
    // The amp of this instance has its circuits built and playing (the second one takes a moment after addParallel).
    bool ampBuilt(int instance) const { return amps_[instance].chain != nullptr; }

    juce::AudioProcessorValueTreeState apvts;

private:
    class ChainBuilder;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout(const std::vector<sas::AmpDef>& defs);
    bool loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name, int slot, int cabinet);
    bool loadNamFromMemory(const void* data, std::size_t size, const juce::String& name);
    bool loadNamCabFromMemory(const void* data, std::size_t size, const juce::String& name);
    bool loadNamPedalFromMemory(const void* data, std::size_t size, const juce::String& name, int instance);
    bool anyIrLoaded(int cabinet = 0) const { return cabs_[cabinet].ir[0].loaded.load() || cabs_[cabinet].ir[1].loaded.load(); }
    double cabValue(int cabinet, const char* id) const;  // a parameter of that cabinet
    sas::ChainSettings readSettings(bool irAvailable, int instance = 0);
    void swapInBuiltChain(int instance);
    double ampValue(int instance, const char* id) const;  // a parameter of the amp of that instance
    friend class CabinetModule;
    // The rack (docs/MODULAR_RACK.md).
    std::unique_ptr<sas::Module> makeModule(sas::ModuleType type, int instance);
    void syncRack(sas::Transition how);
    void insertEntry(int at, sas::SlotRef slot, int lane);
    void eraseEntry(int at);
    int indexInOrder(sas::SlotRef slot) const;
    void normalizeLanes();  // lane 1 only between the splitter and the mixer
    bool layoutOk() const;  // the rules of the parallel hold
    sas::AmpModule* ampModule(int instance = 0) const { return static_cast<sas::AmpModule*>(rack_.audioModule({sas::ModuleType::Amp, instance})); }
    double moduleParam(sas::SlotRef slot, const char* baseId) const;
    void applyModuleParams(const sas::RackOrder& order);
    // The cabinet's work: filter, NAM cabinet or impulse responses, double tracking (called by CabinetModule on the audio thread).
    void processCabinet(class CabinetModule& cab, float* l, float* r, std::size_t n);
    int wantedKey(int instance) const;
    static int makeKey(int amp, const sas::Condition& c) { return amp + 32 * (c.tube + 8 * (c.wear + 16 * c.unit)); }

    std::vector<sas::AmpDef> defs_;

    // Per amp: the APVTS value behind each preamp knob (nullptr = the knob's default) and presence.
    struct AmpParams {
        std::vector<std::atomic<float>*> knobs;
        std::vector<std::atomic<float>*> powerKnobs;
        std::atomic<float>* presence = nullptr;
        std::atomic<float>* vibSpeed = nullptr;
    };
    std::array<std::vector<AmpParams>, 2> ampParams_;  // per instance of the amp, then per amp of the list

    // One per amp of the rack (the second plays only while there is a parallel): the circuits being played, and the next ones, which are built off
    // the audio thread and swapped in between blocks.
    struct AmpRunner {
        std::unique_ptr<sas::Chain> chain;
        std::atomic<int> activeAmp{0};  // the amp the chain was built for (written by the audio thread)
        std::atomic<int> activeKey{0};  // amp + condition the chain was built for; -1: nothing built yet
        std::atomic<int> requestedKey{-1}, readyKey{-1};
        std::atomic<sas::Chain*> ready{nullptr}, retired{nullptr};
        std::unique_ptr<ChainBuilder> builder;
    };
    AmpRunner amps_[2];
    double sampleRate_ = 0.0;
    int latencyReported_ = -1;

    struct IrSlot {
        juce::dsp::Convolution conv{juce::dsp::Convolution::NonUniform{512}};
        std::atomic<bool> loaded{false};
        juce::MemoryBlock bytes;      // the file as loaded, kept for the project state
        juce::String name;
        std::vector<float> curve;     // magnitude response for the editor
    };
    // What one cabinet owns: its two impulse slots, the blend state and the double tracking, and what processBlock tells its module each block.
    struct CabBank {
        IrSlot ir[kIrSlots];
        bool irWasActive = false;
        float blendGainA = 1.0f, blendGainB = 0.0f;  // the gains in force at the end of the last block (ramped to the new ones)
        float widthNow = 0.0f;                       // the A / B width in force at the end of the last block
        sas::StereoDouble dbl;
        sas::NamModel* namNow = nullptr;             // the NAM cabinet this block (only the first cabinet has one)
        bool irActiveNow = false, aLoaded = false, bLoaded = false, wasOn = true;
    };
    CabBank cabs_[2];
    std::vector<float> mono2_, monoR_;  // the second IR's signal, and the right channel (scratch, shared by the cabinets: they run one after another)
    int firstCab_ = 0;                  // the built-in impulse a new cabinet opens with
    mutable juce::CriticalSection irLock_;  // guards the slots' bytes / name / curve (message thread vs state calls)

    // The NAM model: owned here, handed to the chain as a plain pointer (namActive_). The model before the current one is kept for one more
    // load so the audio thread, which may still be inside it, never sees it freed.
    std::shared_ptr<sas::NamModel> nam_, namPrev_;
    std::atomic<sas::NamModel*> namActive_{nullptr};
    juce::MemoryBlock namBytes_;
    juce::String namName_, namMessage_;
    double namRate_ = 0.0;
    mutable juce::CriticalSection namLock_;
    // The NAM cabinet, the same way.
    std::shared_ptr<sas::NamModel> namCab_, namCabPrev_;
    std::atomic<sas::NamModel*> namCabActive_{nullptr};
    juce::MemoryBlock namCabBytes_;
    juce::String namCabName_, namCabMessage_;
    double namCabRate_ = 0.0;
    bool namCabWasActive_ = false;
    // The NAM pedals (one per instance of the module), the same way. The module ramps the gains over each block.
    struct NamPedalSlot {
        std::shared_ptr<sas::NamModel> model, prev;
        std::atomic<sas::NamModel*> active{nullptr};
        juce::MemoryBlock bytes;
        juce::String name, message;
        double rate = 0.0;
    };
    static constexpr int kNamPedalSlots = 2;
    NamPedalSlot namPedals_[kNamPedalSlots];

    sas::Rack rack_;
    sas::RackOrder rackOrder_;
    std::array<std::array<std::vector<std::atomic<float>*>, sas::Rack::kMaxInstances>, sas::kModuleTypeCount> modParams_;
    // What the cabinet module needs this block (set by processBlock).
    sas::SpectrumFeed spectrumFeed_;  // the output, for the picture of what is sounding on the EQ page
    sas::TunerFeed tuner_;
    std::atomic<float> inputPeak_{0.0f}, outputPeak_{0.0f}, gateGain_{1.0f}, idleMa_{0.0f};
    std::vector<float> mono_;
    sas::ChainSettings settings_, settingsB_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasProcessor)
};
