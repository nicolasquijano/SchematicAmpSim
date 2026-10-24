#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

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

class AesEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit AesEditor(AesProcessor&);
    ~AesEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseImpulseResponse();
    void styleCombo(juce::ComboBox&, const char* paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&);
    void styleCaption(juce::Label&, const juce::String&);

    AesProcessor& processor_;
    AmpLookAndFeel look_;

    KnobWithLabel volume_, bass_, middle_, treble_, presence_, master_;
    KnobWithLabel input_, output_;
    KnobWithLabel tsDrive_, tsTone_, tsLevel_;

    juce::ComboBox quality_, jack_, cab_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAtt_, jackAtt_, cabAtt_;
    juce::Label qualityCaption_, jackCaption_, cabCaption_, tsCaption_, irName_;
    juce::ToggleButton tsOn_{"On"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tsOnAtt_;
    juce::TextButton loadIr_{"Load IR..."}, clearIr_{"Clear"};
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AesEditor)
};
