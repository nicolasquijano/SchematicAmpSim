#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "sas/amp_def.h"

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

class SasEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ListBoxModel {
public:
    explicit SasEditor(SasProcessor&);
    ~SasEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void pollMeters();  // reads the processor's peaks into the level bars (the timer calls it)
    void updateImpulseStatus();  // the impulse names and response curves of the Cabinet page (the timer calls it)

private:
    // The amp list (a ListBox over the amp parameter's choices).
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRow) override;

    void timerCallback() override;
    void chooseImpulseResponse(int slot);
    void showAmp(int index);  // rebuilds the amp-specific knobs, jack list and title
    void styleCombo(juce::ComboBox&, const char* paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&);
    void styleCaption(juce::Label&, const juce::String&);
    void setPage(int page);  // 0 = amp, 1 = effects, 2 = cabinet
    void drawLogo(juce::Graphics&, juce::Rectangle<float>) const;
    void applyTheme(const sas::AmpDef&);  // colours of the shown amp

    SasProcessor& processor_;
    AmpLookAndFeel look_;
    int shownAmp_ = -1;
    int page_ = 0;
    juce::String title_;
    juce::StringArray ampNames_;
    struct Panel {
        juce::Rectangle<int> bounds;
        juce::String caption;
    };
    juce::Rectangle<int> logoBounds_;  // the product mark and name, top of the left column
    std::vector<Panel> panels_;  // drawn behind the controls, set by resized()

    std::vector<std::unique_ptr<KnobWithLabel>> ampKnobs_;  // first row, from the amp's "ui" block
    KnobWithLabel input_, output_;
    KnobWithLabel tsDrive_, tsTone_, tsLevel_;
    KnobWithLabel gateThreshold_, gateRelease_, cable_;
    KnobWithLabel bias_, wear_, speaker_, blend_;
    KnobWithLabel compSustain_, compBlend_, compTone_, compLevel_;
    LevelBar inBar_, outBar_;
    TubeGlow tubes_;
    int tick_ = 0;

    juce::ListBox ampList_{"amps", nullptr};
    juce::ComboBox quality_, jack_, cab_, pickup_, tube_, noise_, unit_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt_, cabAtt_, pickupAtt_, noiseAtt_, unitAtt_;
    juce::Label ampCaption_, qualityCaption_, jackCaption_, cabCaption_, pickupCaption_, nameA_, nameB_, blendNote_, cabNote_, tubeCaption_, noiseCaption_, unitCaption_, idleLabel_, conditionNote_;
    juce::ToggleButton tsOn_{"On"};
    juce::ToggleButton gateOn_{"On"};
    juce::ToggleButton speakerIntense_{"Intense"};
    juce::ToggleButton compOn_{"On"}, compFast_{"Fast"};
    juce::ToggleButton levelMatch_{"Level match"};  // shown only for amps whose jacks differ in level
    juce::ToggleButton vibrato_{"Vibrato"};  // shown only for amps that have an optical tremolo
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tsOnAtt_, gateOnAtt_, speakerIntenseAtt_, compOnAtt_, compFastAtt_, levelMatchAtt_, vibratoAtt_;
    juce::TextButton ampTab_{"Amp"}, cabTab_{"Cabinet"}, fxTab_{"Effects"};
    juce::TextButton loadA_{"Load..."}, clearA_{"Clear"}, loadB_{"Load..."}, clearB_{"Clear"};
    IrCurve curveA_, curveB_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasEditor)
};
