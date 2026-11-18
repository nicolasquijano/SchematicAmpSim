#pragma once
// The circuit page: the amp's signal path with a live level for every stage (the guitar, the points named in amp.json "probes" and "power_probes",
// the speaker). An amp that has drawings (amps/<id>/face/circuit.svg for the preamp and circuit_power.svg for the power amp) shows its schematic:
// the wire of each stage takes the colour of its level, the tubes glow with the signal they carry, and the pots are the amp's knobs (drag them). The
// meters are also kept as a strip of bars under it; an amp without a drawing shows the stages as blocks with the bars. The levels come from the
// audio thread (SasProcessor::takeMeter); the editor calls tick() at its own rate.

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

#include "PluginEditor.h"

class CircuitPage final : public juce::Component {
public:
    CircuitPage(SasProcessor& processor, const AmpLookAndFeel& look);
    ~CircuitPage() override;

    // The amp that is shown: its stage names (the first and the last are the guitar and the speaker) and its drawings, if it has them.
    void setAmp(const sas::AmpDef& def);
    // Reads the meters: call it at the editor's timer rate while the page is visible.
    void tick();

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    // The pots of the drawing are the amp's knobs: drag one up or down (or use the wheel) to turn it, double-click to bring it back to its default.
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // For the tests: the level of stage `i` as the page shows it now, 0..1 over its range.
    float shownFraction(int i) const { return i >= 0 && i < int(stages_.size()) ? stages_[std::size_t(i)].shown : 0.0f; }
    int stageCount() const { return int(stages_.size()); }
    juce::String stageName(int i) const { return stages_[std::size_t(i)].name; }
    bool hasDrawing() const { return drawings_[0].art != nullptr; }
    bool hasPowerDrawing() const { return drawings_[1].art != nullptr; }
    // Which drawing is shown: 0 the preamp, 1 the power amp.
    int view() const { return view_; }
    void setView(int v);
    // How many wires of the drawings follow a stage's level, and how many pots they have.
    int wireCount() const { return int(drawings_[0].wires.size() + drawings_[1].wires.size()); }
    int potCount() const { return int(drawings_[0].pots.size() + drawings_[1].pots.size()); }
    // The parameter of pot `i` (the preamp's first, then the power amp's).
    juce::String potParam(int i) const;
    // Turns pot `i` by `delta` of its range (what a drag or the wheel does).
    void nudgePot(int i, float delta);

    // The meters span this many dB below the stage's limit.
    static constexpr float kRangeDb = 48.0f;

private:
    struct Stage {
        juce::String key, name;  // `key` is the id in the drawing ("wire_V1"), `name` what is shown
        float shown = 0.0f, peak = 0.0f;  // 0..1 over the range
        juce::uint32 peakAtMs = 0, clipAtMs = 0;
        float levelDb = -100.0f;
    };
    struct Wire {
        std::size_t stage = 0;
        juce::DrawableShape* shape = nullptr;
    };
    struct Tag {
        juce::Drawable* item = nullptr;
        juce::String text;
        int kind = 0;  // 0 = under it, 1 = to the right
    };
    struct Pot {
        juce::Drawable* body = nullptr;
        juce::DrawablePath* pointer = nullptr;
        juce::String name, param;
        juce::Point<float> centre;  // in the drawing's own coordinates
        float shown = -1.0f;        // the value the pointer is drawn at, 0..1
        float length = 11.0f;       // of the pointer, in the drawing's units
    };
    // One drawing (the preamp or the power amp) and what was found in it.
    struct Drawing {
        std::unique_ptr<juce::Drawable> art;
        juce::AffineTransform fit;
        std::vector<Wire> wires;
        std::map<juce::String, std::vector<juce::DrawableShape*>> glows;  // "V1" -> the glow of that tube
        std::vector<Tag> tags;
        std::vector<Pot> pots;
        std::map<juce::String, juce::DrawablePath*> pointers;
        juce::Drawable* next = nullptr;  // the arrow that leads on to the power amp
        struct Value {
            juce::Drawable* item = nullptr;
            juce::String text;
            char align = 'L';
        };
        std::vector<Value> values;  // "valL_<n>_<text>": the value of a component, in small type (L: starts at the anchor, R: ends there, C: centred)
        std::map<juce::String, juce::Drawable*> labels;  // "lbl_<param>": where a pot's name and value go when not under it
    };
    struct PotRef {
        int drawing = -1, index = -1;
    };

    void load(int which, const sas::AmpDef& def);
    void collect(Drawing& d, juce::Drawable& item);
    PotRef potAt(juce::Point<float> p) const;
    Pot* potOf(PotRef r) { return r.drawing < 0 ? nullptr : &drawings_[std::size_t(r.drawing)].pots[std::size_t(r.index)]; }
    const Pot* potOf(PotRef r) const { return r.drawing < 0 ? nullptr : &drawings_[std::size_t(r.drawing)].pots[std::size_t(r.index)]; }
    void updatePots();
    static float fractionOf(float ratio);
    static juce::Colour levelColour(float db);
    void paintBlocks(juce::Graphics&, juce::Rectangle<float> area);
    void paintStrip(juce::Graphics&, juce::Rectangle<float> area);

    SasProcessor& processor_;
    const AmpLookAndFeel& look_;
    std::vector<Stage> stages_;
    std::array<Drawing, 2> drawings_;
    std::map<juce::String, juce::String> uiLabels_;  // parameter -> the knob's label
    std::vector<juce::String> uiParams_;  // the plugin parameters the amp has on its panel: a pot is live when one has its name
    juce::TextButton viewButtons_[2];
    int view_ = 0;
    PotRef hover_, drag_;
    float dragY_ = 0.0f;
    juce::Rectangle<float> strip_;
};
