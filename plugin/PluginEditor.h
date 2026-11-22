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
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool isButtonDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool highlighted, bool down) override;

    // Typography: one family, small caps-style captions with a little tracking.
    static juce::FontOptions font(float size, bool bold = false, float tracking = 0.0f);

private:
    EditorTheme theme_;
};

// An amp's own switch. A drop-down like any ComboBox, but a switch with two to four short positions is drawn as a row of buttons with the
// chosen one lit, and a click on a position picks it directly (no menu). Long position names keep the drop-down.
class SwitchBox final : public juce::ComboBox {
public:
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    bool segmented() const;
    void hideLabelIfSegmented();  // the drop-down's own text label would draw over the buttons
};

// A knob with its caption above and its value below.
class KnobWithLabel final : public juce::Component {
public:
    // fader: a vertical slider (the graphic EQ bands of the amps that have one) instead of a rotary knob.
    KnobWithLabel(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& caption, bool fader = false);
    void resized() override;
    void lookAndFeelChanged() override;

private:
    juce::Slider slider_{juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow};
    juce::ToggleButton toggle_;  // for an on / off parameter (the NAM loader's EQ position): a switch instead of a knob
    juce::Label caption_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment_;
};

// Vertical level bar (-60..0 dBFS) with a fast rise, a slower fall and a clip mark.
class LevelBar final : public juce::Component {
public:
    void setLevel(float linearPeak);
    float levelDb() const { return db_; }
    void paint(juce::Graphics&) override;  // vertical, or horizontal when it is wider than tall

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

// The equaliser's curve: the response of the eight bands, the high-pass and the low-pass on a log frequency axis, with a draggable node per
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
    // `held`: no note is heard just now and the last one stays on the screen, dimmed, for a moment.
    void setReading(bool valid, bool active, double hz, double referenceA, bool held = false);
    // The level of the input (dBFS) and the sensitivity (dBFS): the bar at the bottom shows both.
    void setLevel(float levelDb, float sensitivityDb);
    // The tuning (an index of sas::kTunings): with strings the display shows them, lights the nearest and ticks off the ones that were in tune.
    void setTuning(int index);
    void paint(juce::Graphics&) override;

private:
    bool valid_ = false, active_ = false;
    double hz_ = 0.0, referenceA_ = 440.0;
    sas::NoteInfo note_;
    float needle_ = 0.0f;  // smoothed cents
    int tuning_ = 0, string_ = -1, steady_ = 0;
    double stringCents_ = 0.0;
    unsigned tuned_ = 0;  // bit per string: it was in tune and held
    bool held_ = false;
    float levelDb_ = -120.0f, sensDb_ = -54.0f;
    // The last few seconds of the pitch (cents from the note, NaN where nothing was heard), oldest at histPos_.
    static constexpr int kHistory = 120;
    std::array<float, kHistory> history_ = [] { std::array<float, kHistory> a; a.fill(std::nanf("")); return a; }();
    int histPos_ = 0;
    void pushHistory(float cents) {
        history_[std::size_t(histPos_)] = cents;
        histPos_ = (histPos_ + 1) % kHistory;
    }
};

// The output tubes drawn as glass envelopes whose plates glow with the plate current (the share of the tube's rating),
// so bias, wear and how hard the amp is driven can be seen.
class TubeGlow final : public juce::Component {
public:
    void setTubes(int count, const juce::String& name);
    // The amps with a transistor output stage show power transistors instead of tubes: no glow, they only sit there.
    void setTransistors(int count, const juce::String& name);
    // `level`: plate dissipation as a fraction of the rating (about 0.7 at a typical idle, above 1 when overdriven); `mA` for the text.
    void setGlow(float level, float mA);
    void paint(juce::Graphics&) override;

private:
    int count_ = 2;
    bool transistors_ = false;
    juce::String name_;
    float level_ = 0.0f, shown_ = 0.0f, base_ = 0.0f, mA_ = 0.0f;  // shown_ follows the current quickly; base_ is the level it has idled at lately
};

class RackPage;
class CircuitPage;
class AmpPanelB;
class CabinetPanelB;

// One box drawn behind the controls of a panel: a rounded rectangle with a small accent mark and a caption.
struct PanelBox {
    juce::Rectangle<int> bounds;
    juce::String caption;
};
void paintPanelBoxes(juce::Graphics&, const AmpLookAndFeel&, const std::vector<PanelBox>&);
// What "in phase" did to the impulses of a cabinet, as a few words for the panel (empty when it did nothing).
juce::String irAlignText(const SasProcessor::IrAlignInfo& info);

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

class MultiPanel;

class SasEditor final : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer, private juce::ListBoxModel {
public:
    explicit SasEditor(SasProcessor&);
    ~SasEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;  // in the standalone window: the maximise button
    void pollMeters();  // reads the processor's peaks into the level bars (the timer calls it)
    void updateTuner();  // runs the pitch detection on the latest input (the timer calls it)
    void updateImpulseStatus();  // the impulse names and response curves of the cabinet unit (the timer calls it)
    void showModule(sas::SlotRef slot);  // opens a module of the rack page
    CircuitPage& circuitPage() { return *circuitPage_; }  // for the tests
    void selectPage(int page) { setPage(page); }
    juce::StringArray pageNames() const { return {rackTab_.getButtonText(), tunerTab_.getButtonText(), circuitTab_.getButtonText()}; }
    // The input and output meters in the header (always there, whatever the page).
    float headerInputDb() const { return headIn_.levelDb(); }
    float headerOutputDb() const { return headOut_.levelDb(); }
    bool headerMetersVisible() const { return headIn_.isVisible() && headOut_.isVisible(); }
    juce::TextButton& headerInputAuto() { return headInAuto_; }
    juce::TextButton& headerOutputAuto() { return headOutAuto_; }
    juce::Slider& headerInputKnob() { return headInKnob_; }  // the input and output levels, in the header with the meters
    juce::Slider& headerOutputKnob() { return headOutKnob_; }
    float totalCpu() const;  // the cost of the modules of the rack, in percent of a block

private:
    // The amp list (a ListBox over the amp parameter's choices).
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRow) override;
    juce::Component* refreshComponentForRow(int row, bool selected, juce::Component* existing) override;  // the star that marks a favourite
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
    void setPage(int page);  // 0 = rack, 1 = tuner, 2 = circuit
    void drawLogo(juce::Graphics&, juce::Rectangle<float>) const;
    void applyTheme(const sas::AmpDef&);  // colours of the shown amp
    // The amp unit and the cabinet unit of the rack page: their controls live in two panels the editor owns.
    void layoutAmpPanel();
    int ampTopHeight() const;   // the knob row of the amp unit: its faceplate, or the knobs (taller when they have section titles)
    void updateKnobActivity();  // dims the knobs the selectors currently make useless (amp.json "active_when")
    void layoutCabPanel();
    int ampPanelHeight() const;
    int cabPanelHeight() const;

    SasProcessor& processor_;
    AmpLookAndFeel look_;
    int shownAmp_ = -1;
    int page_ = 0;
    juce::String title_;
    juce::StringArray ampNames_;
    bool sizeDirty_ = false;  // the window was resized and the new size is not saved yet
    juce::uint32 sizeChangedMs_ = 0;
    bool pickedFromTop_ = false;  // the amp in use was chosen in the favourites rows at the top of the list
    juce::Rectangle<int> logoBounds_;  // the product mark and name, top of the left column
    std::vector<PanelBox> panels_;  // drawn behind the controls of the editor itself (the tuner), set by resized()
    HostPanel ampPanel_, cabPanel_;

    std::vector<std::unique_ptr<KnobWithLabel>> ampKnobs_;  // first row of the amp unit, from the amp's "ui" block
    KnobWithLabel input_, output_;
    KnobWithLabel cable_;
    KnobWithLabel bias_, sag_, wear_, speaker_, blend_;
    KnobWithLabel tunerRef_, tunerSens_, stereoWidth_, doubleAmount_, doubleTime_;
    juce::TextButton presets_{"Presets"};
    void showPresetMenu();
    TunerDisplay tunerDisplay_;
    std::vector<float> tunerBuf_ = std::vector<float>(4096);
    int tunerTicks_ = 0, tunerHold_ = 0, tunerHeld_ = 0;
    double tunerHz_ = 0.0;
    LevelBar inBar_, outBar_;
    LevelBar headIn_, headOut_;  // the same levels, in the header
    juce::Slider headInKnob_{juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox}, headOutKnob_{juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> headInAtt_, headOutAtt_;
    juce::Rectangle<int> headInLabel_, headOutLabel_;
    // The "Auto" buttons: play for a few seconds, then the level knob is set from the loudest peak heard (side 1 = input, 2 = output).
    juce::TextButton headInAuto_{"Auto"}, headOutAuto_{"Auto"};
    int levelSide_ = 0;
    juce::uint32 levelStartMs_ = 0, levelNoteUntilMs_ = 0;
    int levelNoteSide_ = 0;
    void toggleLevelMeasure(int side);
    void finishLevelMeasure();
    void updateLevelButtons();
    TubeGlow tubes_;
    int tick_ = 0;

    juce::ListBox ampList_{"amps", nullptr};
    juce::TextEditor ampSearch_;
    std::vector<int> shown_;  // the rows of the amp list: an amp index that matches the search, or -(group + 1) for the header of a group (clean, crunch, high gain, bass, other)
    juce::ComboBox quality_, jack_, cab_, pickup_, tube_, noise_, unit_, tunerTuning_;
    juce::Label tunerTuningCaption_;
    std::unique_ptr<AmpFace> face_;  // the amp's front, for amps that have a faceplate (replaces the generic knobs)
    SwitchBox selBox_[6];  // the amp's own switches
    juce::Label selCaption_[6];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt_, cabAtt_, pickupAtt_, noiseAtt_, unitAtt_, tunerTuningAtt_;
    juce::Label ampCaption_, qualityCaption_, jackCaption_, cabCaption_, pickupCaption_, nameA_, nameB_, blendNote_, cabNote_, tubeCaption_, noiseCaption_, unitCaption_, idleLabel_, conditionNote_;
    juce::ToggleButton speakerIntense_{"Intense"};
    juce::ToggleButton alignOn_{"In phase"};  // the cabinet's impulses A and B put in phase
    juce::Label alignNote_;
    // The multi impulse loader (cabinet mode MULTI): the plane and the strips on top, the spread and the phase switch in the blend panel's place.
    std::unique_ptr<MultiPanel> multiPanel_;
    std::unique_ptr<KnobWithLabel> multiSpread_;
    juce::ToggleButton multiAlign_{"In phase"};
    juce::Label multiAlignNote_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> multiAlignAtt_;
    bool multiMode_ = false;
    void updateCabMode();
    juce::ToggleButton tunerOn_{"Tuner on"}, tunerMute_{"Mute output"};
    juce::ToggleButton vibrato_{"Vibrato"};  // shown only for amps that have an optical tremolo
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> alignAtt_, speakerIntenseAtt_, tunerOnAtt_, tunerMuteAtt_, vibratoAtt_;
    juce::TextButton rackTab_{"Rack"}, tunerTab_{"Tuner"}, circuitTab_{"Circuit"};
    juce::TextButton loadA_{"Load..."}, clearA_{"Clear"}, loadB_{"Load..."}, clearB_{"Clear"};
    IrCurve curveA_, curveB_;
    juce::TextButton namLoad_{"Load model..."}, namClear_{"Clear"};  // the NAM Loader amp's model
    juce::Label namName_, namNote_;
    juce::TextButton namCabLoad_{"Load NAM..."}, namCabClear_{"Clear"};  // the NAM cabinet of the cabinet unit
    juce::Label namCabName_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<AmpPanelB> ampPanelB_;          // the second amp and cabinet of a parallel (their units host them)
    std::unique_ptr<CabinetPanelB> cabPanelB_;
    juce::Rectangle<int> cpuBounds_, titleBounds_;  // the amp's name: what is left of the header to the left of the level meters
    std::unique_ptr<CircuitPage> circuitPage_;
    std::unique_ptr<RackPage> rackPage_;  // declared last: it goes first, and lets go of the panels

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasEditor)
};
