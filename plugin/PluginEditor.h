#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "AmpFace.h"
#include "PluginProcessor.h"
#include "sas/amp_def.h"
#include "sas/eq.h"
#include "sas/tuner.h"

// The editor's colours for one amp (from the amp's amp.json "theme").
struct EditorTheme {
    juce::Colour background, panel, edge, accent, label, text, knobTop, knobBottom, pointer;
    static EditorTheme from(const sas::ThemeDef& t) {
        auto c = [](std::uint32_t v) { return juce::Colour(0xff000000u | v); };
        return {c(t.background), c(t.panel), c(t.edge), c(t.accent), c(t.label), c(t.text), c(t.knobTop), c(t.knobBottom), c(t.pointer)};
    }
};

// Dark panels with an accent colour per amp: rotary knob with tick marks, a dome cap and a value arc; switches drawn as
// small LED toggles; the list / combo / button colours.
class AmpLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    AmpLookAndFeel();
    void setTheme(const EditorTheme& t);
    const EditorTheme& theme() const { return theme_; }
    juce::Colour accent() const { return theme_.accent; }
    juce::Colour panel() const { return theme_.panel; }
    juce::Colour panelEdge() const { return theme_.edge; }
    juce::Colour label() const { return theme_.label; }
    juce::Colour text() const { return theme_.text; }
    juce::Colour background() const { return theme_.background; }

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    // Typography: one family, small caps-style captions with a little tracking.
    static juce::FontOptions font(float size, bool bold = false, float tracking = 0.0f);

private:
    EditorTheme theme_;
};

// A knob with its caption above and its value below.
class KnobWithLabel final : public juce::Component {
public:
    KnobWithLabel(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& caption);
    void resized() override;
    void lookAndFeelChanged() override;

private:
    juce::Slider slider_{juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow};
    juce::Label caption_;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment_;
};

// Vertical level bar (-60..0 dBFS) with a fast rise, a slower fall and a clip mark.
class LevelBar final : public juce::Component {
public:
    void setLevel(float linearPeak);
    void paint(juce::Graphics&) override;

private:
    float db_ = -90.0f;
    int clip_ = 0;
};

// A small plot of an impulse response's magnitude curve (log frequency 50 Hz..10 kHz, +-15 dB).
class IrCurve final : public juce::Component {
public:
    void setCurve(std::vector<float> curve) {
        if (curve == curve_) return;
        curve_ = std::move(curve);
        repaint();
    }
    void paint(juce::Graphics&) override;

private:
    std::vector<float> curve_;
};

// The equaliser's curve: the response of the five bands, the high-pass and the low-pass on a log frequency axis, with a draggable node per
// band (left / right = frequency, up / down = gain, wheel = Q, double click = gain back to 0).
class EqCurve final : public juce::Component {
public:
    explicit EqCurve(juce::AudioProcessorValueTreeState& apvts, int instance = 0);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int selected() const { return selected_; }
    // The levels (dB at log-spaced points from 20 Hz to 20 kHz) of what is sounding, drawn behind the curve; null = nothing to draw.
    void setSpectrum(const std::array<float, sas::SpectrumAnalyzer::kPoints>* db) { spectrum_ = db; }
    void select(int band);
    std::function<void(int)> onSelect;

private:
    sas::EqParams params() const;
    float xOf(double hz) const;
    float yOf(double db) const;
    double hzAt(float x) const;
    double dbAt(float y) const;
    int nodeAt(juce::Point<float> p) const;
    void setPlain(const juce::String& id, double value, bool gesture);
    juce::String pid(const char* baseId) const;  // the id of this instance's parameter

    juce::AudioProcessorValueTreeState& apvts_;
    int instance_ = 0;
    int selected_ = 2, dragging_ = -1;
    const std::array<float, sas::SpectrumAnalyzer::kPoints>* spectrum_ = nullptr;
};

// The tuner's face: the note, how far from it in cents on a needle meter, and the frequency.
class TunerDisplay final : public juce::Component {
public:
    // `valid`: a pitch was found; `active`: the tuner is on at all.
    void setReading(bool valid, bool active, double hz, double referenceA);
    void paint(juce::Graphics&) override;

private:
    bool valid_ = false, active_ = false;
    double hz_ = 0.0;
    sas::NoteInfo note_;
    float needle_ = 0.0f;  // smoothed cents
};

// The output tubes drawn as glass envelopes whose plates glow with the plate current (the share of the tube's rating),
// so bias, wear and how hard the amp is driven can be seen.
class TubeGlow final : public juce::Component {
public:
    void setTubes(int count, const juce::String& name);
    // `level`: plate dissipation as a fraction of the rating (about 0.7 at a typical idle, above 1 when overdriven); `mA` for the text.
    void setGlow(float level, float mA);
    void paint(juce::Graphics&) override;

private:
    int count_ = 2;
    juce::String name_;
    float level_ = 0.0f, shown_ = 0.0f, mA_ = 0.0f;
};

class RackPage;

// One box drawn behind the controls of a panel: a rounded rectangle with a small accent mark and a caption.
struct PanelBox {
    juce::Rectangle<int> bounds;
    juce::String caption;
};
void paintPanelBoxes(juce::Graphics&, const AmpLookAndFeel&, const std::vector<PanelBox>&);

// A container for the controls of one big unit (the amp, the cabinet). The editor owns it and lays the controls out; a unit of the rack page
// hosts it while it is on show.
class HostPanel final : public juce::Component {
public:
    void paint(juce::Graphics& g) override {
        if (const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel())) paintPanelBoxes(g, *look, panels);
    }
    void resized() override {
        if (onResized) onResized();
        repaint();
    }
    std::vector<PanelBox> panels;
    std::function<void()> onResized;
};

class SasEditor final : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer, private juce::ListBoxModel {
public:
    explicit SasEditor(SasProcessor&);
    ~SasEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void pollMeters();  // reads the processor's peaks into the level bars (the timer calls it)
    void updateTuner();  // runs the pitch detection on the latest input (the timer calls it)
    void updateImpulseStatus();  // the impulse names and response curves of the cabinet unit (the timer calls it)
    juce::StringArray pageNames() const { return {rackTab_.getButtonText(), tunerTab_.getButtonText()}; }

private:
    // The amp list (a ListBox over the amp parameter's choices).
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRow) override;
    void refreshAmpList();  // applies the search box to the list (the rows are positions in shown_)

    void timerCallback() override;
    void chooseImpulseResponse(int slot);
    void chooseNamModel();
    void chooseNamCab();
    void refreshNamCab();
    void refreshNam();
    void showImpulseMenu(int slot, juce::Component& anchor);  // the impulses built into the plugin, and "From file..."
    void showAmp(int index);  // rebuilds the amp-specific knobs, jack list and title
    void styleCombo(juce::ComboBox&, const char* paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&);
    void styleCaption(juce::Label&, const juce::String&);
    void setPage(int page);  // 0 = rack, 1 = tuner
    void drawLogo(juce::Graphics&, juce::Rectangle<float>) const;
    void applyTheme(const sas::AmpDef&);  // colours of the shown amp
    // The amp unit and the cabinet unit of the rack page: their controls live in two panels the editor owns.
    void layoutAmpPanel();
    void layoutCabPanel();
    int ampPanelHeight() const;
    int cabPanelHeight() const;

    SasProcessor& processor_;
    AmpLookAndFeel look_;
    int shownAmp_ = -1;
    int page_ = 0;
    juce::String title_;
    juce::StringArray ampNames_;
    juce::Rectangle<int> logoBounds_;  // the product mark and name, top of the left column
    std::vector<PanelBox> panels_;  // drawn behind the controls of the editor itself (the tuner), set by resized()
    HostPanel ampPanel_, cabPanel_;

    std::vector<std::unique_ptr<KnobWithLabel>> ampKnobs_;  // first row of the amp unit, from the amp's "ui" block
    KnobWithLabel input_, output_;
    KnobWithLabel cable_;
    KnobWithLabel bias_, wear_, speaker_, blend_;
    KnobWithLabel tunerRef_, stereoWidth_, doubleAmount_, doubleTime_;
    juce::TextButton presets_{"Presets"};
    void showPresetMenu();
    TunerDisplay tunerDisplay_;
    std::vector<float> tunerBuf_ = std::vector<float>(3072);
    int tunerTicks_ = 0, tunerHold_ = 0;
    double tunerHz_ = 0.0;
    LevelBar inBar_, outBar_;
    TubeGlow tubes_;
    int tick_ = 0;

    juce::ListBox ampList_{"amps", nullptr};
    juce::TextEditor ampSearch_;
    std::vector<int> shown_;  // the amp indices that match the search, in list order
    juce::ComboBox quality_, jack_, cab_, pickup_, tube_, noise_, unit_;
    std::unique_ptr<AmpFace> face_;  // the amp's front, for amps that have a faceplate (replaces the generic knobs)
    juce::ComboBox selBox_[4];  // the amp's own switches
    juce::Label selCaption_[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt_, cabAtt_, pickupAtt_, noiseAtt_, unitAtt_;
    juce::Label ampCaption_, qualityCaption_, jackCaption_, cabCaption_, pickupCaption_, nameA_, nameB_, blendNote_, cabNote_, tubeCaption_, noiseCaption_, unitCaption_, idleLabel_, conditionNote_;
    juce::ToggleButton speakerIntense_{"Intense"};
    juce::ToggleButton tunerOn_{"Tuner on"}, tunerMute_{"Mute output"};
    juce::ToggleButton vibrato_{"Vibrato"};  // shown only for amps that have an optical tremolo
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> speakerIntenseAtt_, tunerOnAtt_, tunerMuteAtt_, vibratoAtt_;
    juce::TextButton rackTab_{"Rack"}, tunerTab_{"Tuner"};
    juce::TextButton loadA_{"Load..."}, clearA_{"Clear"}, loadB_{"Load..."}, clearB_{"Clear"};
    IrCurve curveA_, curveB_;
    juce::TextButton namLoad_{"Load model..."}, namClear_{"Clear"};  // the NAM Loader amp's model
    juce::Label namName_, namNote_;
    juce::TextButton namCabLoad_{"Load NAM..."}, namCabClear_{"Clear"};  // the NAM cabinet of the cabinet unit
    juce::Label namCabName_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<RackPage> rackPage_;  // declared last: it goes first, and lets go of the two panels

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasEditor)
};
