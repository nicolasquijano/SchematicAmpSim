#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"

// Gold-on-black rotary knob, the look of the Rust build's egui editor.
class AmpLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    AmpLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    static juce::Colour gold() { return juce::Colour::fromRGB(212, 175, 55); }
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

class SasEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SasEditor(SasProcessor&);
    ~SasEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseImpulseResponse();
    void showAmp(int index);  // rebuilds the amp-specific knobs, jack list and title
    void styleCombo(juce::ComboBox&, const char* paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&);
    void styleCaption(juce::Label&, const juce::String&);

    SasProcessor& processor_;
    AmpLookAndFeel look_;
    int shownAmp_ = -1;
    juce::String title_;

    std::vector<std::unique_ptr<KnobWithLabel>> ampKnobs_;  // first row, from the amp's "ui" block
    KnobWithLabel input_, output_;
    KnobWithLabel tsDrive_, tsTone_, tsLevel_;

    juce::ComboBox amp_, quality_, jack_, cab_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampAtt_, qualityAtt_, cabAtt_;
    juce::Label ampCaption_, qualityCaption_, jackCaption_, cabCaption_, tsCaption_, irName_;
    juce::ToggleButton tsOn_{"On"};
    juce::ToggleButton levelMatch_{"Level match"};  // shown only for amps whose jacks differ in level
    juce::ToggleButton vibrato_{"Vibrato"};  // shown only for amps that have an optical tremolo
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tsOnAtt_, levelMatchAtt_, vibratoAtt_;
    juce::TextButton loadIr_{"Load IR..."}, clearIr_{"Clear"};
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SasEditor)
};
