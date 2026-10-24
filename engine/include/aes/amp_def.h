#pragma once
// Everything that defines one amp, loaded from files: amp.json (knobs, jacks, supply, Lite fit)
// plus the preamp and power-amp netlists it names. Adding an amp means adding such a folder.

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aes {

enum class Taper { Linear, Log };

struct PotDef {
    enum class Type { Wiper, Variable } type = Type::Wiper;
    std::string top, bottom;  // Wiper: sections above and below the wiper
    std::string resistor;     // Variable: the single resistor
    double total = 0;
    Taper taper = Taper::Linear;
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
};

struct SupplyDef {
    double mainsHz = 50, vacPeak = 538, rectSourceOhms = 42, htDcSource = 490, rectDrop = 1.4,
           reservoirFarads = 100e-6, tieOhms = 1.0, settleSeconds = 0.15;
};

// A control that lives in the power amp: one resistor of its netlist set to scale * value
// (value 0..1), e.g. a negative-feedback "power" pot used as a variable resistor.
struct PowerKnobDef {
    std::string name, resistor;
    double scale = 100e3, minOhms = 50, def = 0.5;
};

struct PowerDef {
    std::vector<PowerKnobDef> knobs;
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
    std::vector<UiKnob> ui;
    PreampDef preamp;
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

}  // namespace aes
