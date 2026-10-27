#pragma once
// Everything that defines one amp, loaded from files: amp.json (knobs, jacks, supply, Lite fit)
// plus the preamp and power-amp netlists it names. Adding an amp means adding such a folder.

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

// A later part of the preamp, solved separately: its source `linkSource` is driven every sample
// with the voltage of node `linkFrom` of the previous part (one-way coupling: the later part does
// not load the earlier one, so the cut belongs at a low-impedance node such as a cathode follower).
// The netlist's S element sets the Thevenin resistance of the link.
struct PreampStageDef {
    std::string netlist, netlistText;
    std::string linkFrom, linkSource;
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

struct PreampDef {
    std::vector<PreampStageDef> stages;  // after the first netlist, in signal order
    std::string netlist;       // file name
    std::string netlistText;   // its content
    double tolerance = 2e-6;
    std::string railSource, inputSource, outputNode;
    double potMinOhms = 100;
    std::vector<KnobDef> knobs;
    std::vector<std::string> sourceResistors;
    double sourceOhmsDefault = 7000, sourceOhmsMin = 10;
    std::vector<JackDef> jacks;
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

struct PowerDef {
    std::vector<PowerKnobDef> knobs;
    std::vector<PowerTubeDef> tubes;
    BiasDef bias;
    std::string netlist, netlistText;
    double tolerance = 1e-4;
    std::string inputSource, htSource, htNode, railNode, outputNode, loadResistor, nfbResistor;
    std::string presenceResistor;
    // Optional second section of a presence pot: kept at (scale - top), so the pot total is constant.
    std::string presenceComplement;
    double presenceScale = 22e3, presenceMinOhms = 50, presenceDefault = 0.5;
    SupplyDef supply;
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
};

struct AmpDef {
    std::string id, name;
    // Position in the plugin's amp list (then by id). Saved projects store the list index, so an
    // amp keeps its `order` forever and new amps take the next number.
    int order = 1000;
    // Output trim in dB, set so that every amp plays at the same loudness at its default knobs
    // (see tools/sas_match_levels). Applied by the chain, not part of the circuit.
    double outputDb = 0.0;
    std::vector<UiKnob> ui;
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
