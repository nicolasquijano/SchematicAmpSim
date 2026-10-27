#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"

// Dark panels, amber accent: rotary knob with a value arc, and the list / combo / button colours.
class AmpLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    AmpLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    static juce::Colour accent() { return juce::Colour::fromRGB(240, 160, 40); }
    static juce::Colour panel() { return juce::Colour::fromRGB(27, 23, 19); }
    static juce::Colour panelEdge() { return juce::Colour::fromRGB(62, 50, 36); }
};

// A knob with its caption above and its value below.
class KnobWithLabel final : public juce::Component {
public:
    KnobWithLabel(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& caption);
    void resized() override;

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

class SasEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ListBoxModel {
public:
    explicit SasEditor(SasProcessor&);
    ~SasEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void pollMeters();  // reads the processor's peaks into the level bars (the timer calls it)

private:
    // The amp list (a ListBox over the amp parameter's choices).
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRow) override;

    void timerCallback() override;
    void chooseImpulseResponse();
    void showAmp(int index);  // rebuilds the amp-specific knobs, jack list and title
    void styleCombo(juce::ComboBox&, const char* paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&);
    void styleCaption(juce::Label&, const juce::String&);
    void setPage(int page);  // 0 = amp, 1 = effects

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
    std::vector<Panel> panels_;  // drawn behind the controls, set by resized()

    std::vector<std::unique_ptr<KnobWithLabel>> ampKnobs_;  // first row, from the amp's "ui" block
    KnobWithLabel input_, output_;
    KnobWithLabel tsDrive_, tsTone_, tsLevel_;
    KnobWithLabel gateThreshold_, gateRelease_, cable_;
    KnobWithLabel bias_, wear_;
    LevelBar inBar_, outBar_;
    int tick_ = 0;

    juce::ListBox ampList_{"amps", nullptr};
    juce::ComboBox quality_, jack_, cab_, pickup_, tube_, noise_, unit_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt_, cabAtt_, pickupAtt_, noiseAtt_, unitAtt_;
    juce::Label ampCaption_, qualityCaption_, jackCaption_, cabCaption_, pickupCaption_, irName_, tubeCaption_, noiseCaption_, unitCaption_, idleLabel_, conditionNote_;
    juce::ToggleButton tsOn_{"On"};
    juce::ToggleButton gateOn_{"On"};
    juce::ToggleButton levelMatch_{"Level match"};  // shown only for amps whose jacks differ in level
    juce::ToggleButton vibrato_{"Vibrato"};  // shown only for amps that have an optical tremolo
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tsOnAtt_, gateOnAtt_, levelMatchAtt_, vibratoAtt_;
    juce::TextButton ampTab_{"Amp"}, fxTab_{"Effects"};
    juce::TextButton loadIr_{"Load IR..."}, clearIr_{"Clear"};
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasEditor)
};
