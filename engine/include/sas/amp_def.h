#pragma once
// Everything that defines one amp, loaded from files: amp.json (knobs, jacks, supply, Lite fit)
// plus the preamp and power-amp netlists it names. Adding an amp means adding such a folder.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sas {

// Log is the ~15%-at-mid audio taper; Log10 and Log30 are the Fender "10A" and "30A" tapers (10% / 30% at mid).
enum class Taper { Linear, Log, Log10, Log30, RevLog };  // RevLog: the mirror image of Log (a reverse-audio "C" taper)

struct PotDef {
    enum class Type { Wiper, Variable } type = Type::Wiper;
    std::string top, bottom;  // Wiper: sections above and below the wiper
    std::string resistor;     // Variable: the single resistor
    double total = 0;
    Taper taper = Taper::Linear;
    // Further pots of the same value that turn with this one (ganged: one set of tone controls
    // serving two channels): (top, bottom) for a wiper pot, (resistor, "") for a variable one.
    std::vector<std::pair<std::string, std::string>> also;
};

struct KnobDef {
    std::string name;
    double def = 0.5;
    PotDef pot;
};

struct JackDef {
    std::string name;
    std::string label;                          // shown in the editor ("AFD"); defaults to `name`
    // Gain applied at the amp's input while "level match" is on, to even out jacks of very different
    // sensitivity (the JCM800's Low input skips a gain stage and is ~31 dB quieter than High).
    double levelDb = 0.0;
    std::string drive;                          // source that carries the guitar for this jack
    std::vector<std::pair<std::string, double>> resistors;  // switch resistors to set
};

// A switch with several positions (the Ultra Hi / Ultra Lo switches of a bass amp, a mid-range selector...): in each position some switch
// resistors take the given values. Position 0 must be what the netlist has by default. The editor shows one drop-down per selector.
struct SelectorPositionDef {
    std::string label;
    std::vector<std::pair<std::string, double>> resistors;
    std::vector<std::pair<std::string, double>> potTotals;  // knob name -> the total resistance of its pot in this position (a different pot fitted)
    double nfbOhms = 0.0;  // > 0: the power amp's feedback resistor (power.nfb_resistor) takes this value in this position (a channel with its feedback switched off)
    double levelDb = 0.0;  // gain at the amp's input in this position (a channel that is far hotter or quieter than the others: it keeps its gain knob's range useful)
};
struct SelectorDef {
    static constexpr std::size_t kMax = 6;  // how many selectors an amp can have (the plugin has that many parameters)
    std::string name, label;
    std::vector<SelectorPositionDef> positions;
};

// A later part of the preamp, solved separately: its source `linkSource` is driven every sample
// with the voltage of node `linkFrom` of the previous part (one-way coupling: the later part does
// not load the earlier one, so the cut belongs at a low-impedance node such as a cathode follower).
// The netlist's S element sets the Thevenin resistance of the link.
// A stage may also hang off an earlier stage other than the previous one (`links`: `stage` counts the main preamp as 0) and several
// at once, and it may be fed by the guitar itself (`input`: its own `input_source` is driven like the first stage's, which is how two
// parallel channels share the guitar). `linkFrom`/`linkSource` stay the shorthand for "from the previous stage"; both may be empty.
struct PreampLinkDef {
    std::size_t stage = 0;
    std::string from, source;
};
struct PreampStageDef {
    std::string netlist, netlistText;
    std::string linkFrom, linkSource;
    std::vector<PreampLinkDef> links;
    bool input = false;
    // Only these jacks use the stage (a channel's own tubes): it is not solved while another jack is selected. Empty = always.
    std::vector<std::string> jacks;
    // Likewise for selector positions (a channel switch): the stage runs only when every listed selector is at one of its listed positions.
    std::vector<std::pair<std::string, std::vector<int>>> whenSelector;
};

// Optical tremolo (the Fender "vibrato"): a small oscillator circuit of its own, solved at a low rate
// (one step per `controlSamples` audio samples), whose LED current drives a photocell. The photocell
// is a resistor of the preamp netlist, set from the LED current through a one-pole attack/release.
struct VibratoDef {
    bool present = false;
    std::string netlist, netlistText;
    std::string railSource;
    std::string ledFrom, ledTo;  // LED current = (V(ledFrom) - V(ledTo)) / ledOhms
    double ledOhms = 100e3;
    std::string switchResistor;  // footswitch: on_ohms / off_ohms
    double switchOnOhms = 1.0, switchOffOhms = 1e12;
    std::string speedResistor;   // speed pot (a rheostat in the oscillator)
    double speedTotal = 1e6, speedDefault = 0.5;
    Taper speedTaper = Taper::Linear;
    bool speedInvert = false;    // turning the knob up lowers the resistance
    std::string cellResistor;    // photocell, in the preamp netlist
    double cellDarkOhms = 2e6, cellOhmsAt1mA = 4e3, cellExponent = 0.8;
    double cellAttackS = 0.004, cellReleaseS = 0.08;
    std::size_t controlSamples = 64;
};

// Spring reverb: the driver tube and transformer, the pan and the recovery tube. The tubes are a small
// circuit of their own (`netlist`), solved every sample beside the preamp: its source `gridSource` is
// driven from the preamp node `gridNode` (one-way: the driver only taps the signal), and the plate
// signal of `outNode` drives the preamp source `mainSource`. The pan between them is a behavioural
// tank: the voltage of `tankInNode` (drive transformer secondary) goes in, the result drives the
// source `tankOutSource` (recovery tube grid). Each spring is a feedback loop of delay, dispersion
// (all-pass chain) and loss; both are summed.
struct SpringDef {
    double delayMs = 35.0;     // one trip along the spring
    std::size_t sections = 80; // all-pass sections of the dispersion chain
    double coefficient = -0.5; // their coefficient (negative: lows are delayed more than highs)
    double loopGain = 0.85;    // gain of one round trip at low frequencies
    double lossHz = 3000.0;    // low-pass in the loop (the spring loses highs each trip)
};

struct ReverbDef {
    bool present = false;
    std::string netlist, netlistText, railSource;
    std::string gridSource, gridNode, outNode, mainSource;
    std::string tankInNode, tankOutSource;
    double gain = 1.0;         // volts out per volt in, averaged around 1 kHz
    double inputLowHz = 80.0, inputHighHz = 4200.0, outputHighHz = 3500.0;
    double tankRateHz = 16000.0;
    std::vector<SpringDef> springs;
};

// A circuit in front of the preamp, solved on its own and only while its selector is at position 1 (the Kerry King's "Beast" input stage). Its output
// drives the preamp's input source; `detectNode` feeds a downward expander (the noise gate) whose threshold is the knob `gateKnob` (that knob's
// resistor only has to exist: the gate itself is not a circuit).
struct PreStageDef {
    bool present = false;
    std::string netlist, netlistText, inputSource, outputNode, detectNode, gateKnob, selector;
};

struct PreampDef {
    PreStageDef pre;
    std::vector<PreampStageDef> stages;  // after the first netlist, in signal order
    std::vector<std::string> mainJacks;  // like PreampStageDef::jacks, for the first netlist ("main_jacks"); empty = always
    std::vector<std::pair<std::string, std::vector<int>>> mainWhenSelector;  // "main_when_selector"
    std::string netlist;       // file name
    std::string netlistText;   // its content
    double tolerance = 2e-6;
    std::string railSource, inputSource, outputNode;
    double potMinOhms = 100;
    std::vector<KnobDef> knobs;
    std::vector<std::string> sourceResistors;
    double sourceOhmsDefault = 7000, sourceOhmsMin = 10;
    std::vector<JackDef> jacks;
    std::vector<SelectorDef> selectors;
    std::string jackLabel = "INPUT JACK";  // caption of the selector: "MODE", "CHANNEL", ...
    VibratoDef vibrato;
    ReverbDef reverb;
};

struct SupplyDef {
    double mainsHz = 50, vacPeak = 538, rectSourceOhms = 42, htDcSource = 490, rectDrop = 1.4,
           reservoirFarads = 100e-6, tieOhms = 1.0, settleSeconds = 0.15;
    // Optional vacuum-tube rectifier: instead of the constant `rectDrop`, each conducting plate drops
    // tubeVolts * (i / tubeAmps)^(2/3) (Child-Langmuir space charge), in series with rectSourceOhms.
    // 0 = a solid-state bridge (the constant drop).
    double tubeVolts = 0.0, tubeAmps = 0.125;
};

// A control that lives in the power amp: one resistor of its netlist set to scale * value
// (value 0..1), e.g. a negative-feedback "power" pot used as a variable resistor.
struct PowerKnobDef {
    std::string name, resistor;
    double scale = 100e3, minOhms = 50, def = 0.5;
    Taper taper = Taper::Linear;  // the pot's law: the resistor is scale * taper(value)
    std::string complement;       // optional: the other section of a wiper pot, set to scale minus the resistor
};

// A power-tube type the amp can be fitted with (amp.json "power.tubes"; the first entry is the stock one).
// `model` replaces the model name on every `P` line of the power netlist; `biasScale` multiplies the bias
// supply so that the idle current is the same fraction of the tube's `pmaxW` rating as the stock one
// (measured with tools/sas_bias_cal).
struct PowerTubeDef {
    std::string name, model;
    double pmaxW = 25.0, biasScale = 1.0;
};

// The bias of the output tubes: with a fixed bias supply the source `source` of the power
// netlist is scaled by the bias control (cathode-bias amps leave it empty), and the idle current of each output tube is read from the voltage
// across the output transformer's primary resistances `idleResistors` (`tubesPerHalf` tubes share each).
struct BiasDef {
    std::string source;
    std::vector<std::string> idleResistors;
    double tubesPerHalf = 2.0;
    bool adjustable() const { return !source.empty(); }  // cathode-bias amps have no source: they only read the idle current
};

// The loudspeaker as the output transformer sees it: the voice coil's resistance and inductance in series with the cone's mechanical
// resonance (a parallel R-L-C). Everything scales with the load resistor of the power netlist (its nominal impedance), so one set of
// fractions serves every tap. Typical figures of a 12 inch guitar speaker, not measurements of a particular one.
struct SpeakerLoadDef {
    bool enabled = true;
    double reFraction = 0.78;      // DC resistance / nominal impedance
    double leMhPerOhm = 0.09;      // voice-coil inductance, millihenries per ohm of nominal impedance
    double peakFraction = 3.5;     // impedance added by the resonance at its peak, / nominal impedance
    double resonanceHz = 80.0;
    double q = 4.0;                // mechanical Q of the resonance
};

// Parasitics of the output transformer's primary, across its two halves (plate to plate): the winding capacitance, which makes the
// transformer resonate in the treble, and the core loss, a resistor in parallel. Typical figures of a guitar output transformer.
struct TransformerParasiticsDef {
    bool enabled = true;
    double primaryPf = 150.0;      // winding capacitance across the whole primary, picofarads
    double coreLossOhms = 40e3;    // equivalent core-loss resistance across the primary: about 21 times the plate-to-plate impedance (it also damps the winding resonance)
};

struct PowerDef {
    std::vector<PowerKnobDef> knobs;
    std::vector<PowerTubeDef> tubes;
    // An amp with no power tubes has a transistor output stage: how many output transistors the editor draws in place of the tubes ("output_transistors").
    int outputTransistors = 2;
    BiasDef bias;
    std::string netlist, netlistText;
    double tolerance = 1e-4;
    std::string inputSource, htSource, htNode, railNode, outputNode, loadResistor, nfbResistor;
    std::string presenceResistor;
    // Optional second section of a presence pot: kept at (scale - top), so the pot total is constant.
    std::string presenceComplement;
    double presenceScale = 22e3, presenceMinOhms = 50, presenceDefault = 0.5;
    SupplyDef supply;
    SpeakerLoadDef speakerLoad;
    TransformerParasiticsDef transformer;
};

// Behavioural power amp fitted to measurements of the full circuit (see poweramp_lite.h).
struct LiteParams {
    double baseGainDb = 25.72, highPassHz = 21.9, shelfHz = 564.0;
    std::vector<double> presenceDb{0.0, 0.21, 0.58, 1.42, 5.06};
    std::vector<std::pair<double, double>> sagTable{{0.0, 0.0}, {307.0, 60.1}};
    double railXIdle = 391.0, railSagFraction = 0.58;
    double ceilingLight = 66.5, ceilingHeavy = 48.5, ceilingDecay = 119.7, knee = 6.44;
    double sagAttackS = 0.010, sagReleaseS = 0.060;
};

// One knob on the plugin's panel. `param` is the plugin-level parameter slot that drives it
// (volume, volume2, bass, middle, treble, presence, master); `knob` names the preamp knob it moves
// (empty for presence, which belongs to the power amp).
struct UiKnob {
    std::string param, knob, powerKnob, label;  // `knob` (preamp) or `powerKnob` (power amp), or neither (presence)
    std::string sub;                            // a second line under the label on a faceplate ("VOLUME")
    std::string group;                          // section title over this knob (neighbouring knobs with the same title share one block); empty = no section
    std::string style;                          // "slider": a vertical fader (a graphic EQ band) instead of a knob; empty = knob
    std::vector<std::pair<std::string, std::vector<int>>> activeWhen;  // "active_when": selector name -> positions in which the knob does something; the editor dims it in the others
};

// The amp's own front, drawn from the images in amps/<id>/face/ (background.png at twice the size of `art`, knob.png: the knob pointing up).
// Everything below is in the points of the artwork (the same units as `art`).
struct FaceplateDef {
    std::string style;                              // "image"; empty = no faceplate
    double artW = 0, artH = 0;                      // size of the artboard the images were drawn on
    double knobBox = 0;                             // side of the square knob.png covers
    std::vector<std::pair<double, double>> knobs;   // centre of each knob, in the order of `ui`
    std::vector<std::pair<double, double>> jacks;   // centre of each drawn input jack
    std::vector<std::vector<int>> jackValues;       // for each drawn jack: the jack parameter values it stands for (the first is chosen by a click, any of them lights it); default {its index}
    double jackRadius = 0;                          // radius of the ring that marks the jack in use
};

// Colours of the editor while this amp is shown (amp.json "theme", each "#rrggbb"; missing ones keep the amber default).
// Presentation only: nothing in the circuits reads it.
struct ThemeDef {
    std::uint32_t background = 0x0f0d0b, panel = 0x1b1713, edge = 0x3e3224, accent = 0xf0a028, label = 0x968062, text = 0xe6e0d4;
    std::uint32_t knobTop = 0x3a322a, knobBottom = 0x16130f, pointer = 0xffffff;  // the knob cap's gradient and its pointer
};

// A factory preset of an amp (amp.json "presets"): the controls of the panel, the channel (jack) and the switches, as one setting. `params` are the
// plugin parameter slots of "ui" (volume, volume2, bass, middle, treble, presence, master...) on the panel's 0..10 scale; a slot a preset leaves out keeps
// its value. The jack is named as in "jacks" (empty: unchanged) and a selector is set by its name and the index of the position.
struct AmpPresetDef {
    std::string name, jack;
    std::vector<std::pair<std::string, double>> params;
    std::vector<std::pair<std::string, int>> selectors;
};

// A voicing of the power amp (amp.json "power_voicings"): a named choice that changes some parts of the power amp's netlist. Each position gives the
// factors those parts are multiplied by; position 0 changes nothing (the amp as drawn). The editor shows one drop-down per voicing on the power amp's
// drawing; a choice is saved as part edits named "P:<element>" (see Condition::parts), so it is part of the project and of the user's amp presets.
struct PowerVoicingPositionDef {
    std::string label;
    std::vector<std::pair<std::string, double>> factors;  // netlist element of the power amp -> factor on its value
};
struct PowerVoicingDef {
    std::string name, label;
    std::vector<PowerVoicingPositionDef> positions;
};

// A graphic equaliser inside the amp (amp.json "graphic_eq"): fixed bands, each +-rangeDb with a Q; the sliders are plugin parameters (aeq1..) listed in "ui".
struct GraphicEqBand {
    double hz = 1000.0, q = 1.0, rangeDb = 12.0;
};
struct GraphicEqDef {
    std::vector<GraphicEqBand> bands;
};

// A point of the preamp whose level the editor's circuit page shows (amp.json "probes"): the signal at `node` (looked up in the preamp's netlists,
// whichever one has it) with its DC removed, compared to `fullScale` volts. The input and the speaker output are measured by the chain itself.
struct ProbeDef {
    std::string label, node;
    double fullScale = 1.0;
};

// A channel the circuit page can show on its own (amp.json "circuit_channels"): it is current while the amp's input jack is one of `jacks` (indices of
// preamp.jacks) or, with `selector`, while that selector is at one of `positions`. The drawings are circuit.svg (the first channel), circuit_1.svg, circuit_2.svg...
struct CircuitChannelDef {
    std::string label, selector;
    std::vector<int> jacks, positions;  // with `selector`: the positions it is current in
};

struct AmpDef {
    std::string id, name;
    ThemeDef theme;
    // Position in the plugin's amp list (then by id). Saved projects store the list index, so an
    // amp keeps its `order` forever and new amps take the next number.
    int order = 1000;
    // How the amp is grouped in the editor's list (amp.json "category"): "clean", "crunch", "highgain", "bass" or "other" (the default).
    std::string category = "other";
    // Output trim in dB, set so that every amp plays at the same loudness at its default knobs
    // (see tools/sas_match_levels). Applied by the chain, not part of the circuit.
    double outputDb = 0.0;
    std::vector<UiKnob> ui;
    GraphicEqDef graphicEq;             // empty: the amp has none
    std::vector<AmpPresetDef> presets;  // factory settings, in the order of the menu (may be empty)
    std::vector<PowerVoicingDef> powerVoicings;  // choices that change parts of the power amp (may be empty)
    std::vector<CircuitChannelDef> circuitChannels;  // empty: one drawing for the whole preamp
    std::vector<ProbeDef> powerProbes;  // the same for the full power amp's netlist ("power_probes"): after the preamp's in the circuit page; Lite and Eco have no such nodes
    std::vector<ProbeDef> probes;       // the stages of the circuit page, in signal order (may be empty: the page then shows input, preamp and output only)
    // A loader for Neural Amp Modeler models ("nam": true in amp.json): no circuits at all, the chain runs the model the user loaded. Its
    // knobs ("ui") are only the plugin parameters it reads (input drive and output level).
    bool nam = false;
    FaceplateDef faceplate;  // the amp's own front (plugin/AmpFace.cpp) instead of the generic knob panel; style empty = generic
    PreampDef preamp;
    // Optional finer-split preamp for the Eco modes ("eco_preamp" in amp.json: it overrides the
    // netlist and stages of `preamp` and inherits everything else).
    bool hasEcoPreamp = false;
    PreampDef ecoPreamp;
    PowerDef power;
    LiteParams lite;

    std::size_t knobIndex(const std::string& name) const;  // throws if unknown
    std::size_t powerKnobIndex(const std::string& name) const;
    std::size_t jackIndex(const std::string& name) const;
};

using FileReader = std::function<std::string(const std::string& fileName)>;

// Parses amp.json text and loads the netlists it references through `read`.
AmpDef loadAmpDef(const std::string& jsonText, const FileReader& read);
// Same, reading all files from a folder.
AmpDef loadAmpDefFromFolder(const std::string& folder);

}  // namespace sas
