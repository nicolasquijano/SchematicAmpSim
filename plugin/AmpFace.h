#pragma once
// The front of an amp drawn on the Amp page from the artwork in amps/<id>/face/ (background.png and knob.png, see sas::FaceplateDef): the panel
// image fills the area, the knobs are the plugin parameters drawn as the rotated knob image, and the input jacks (when there are several) select
// the jack parameter. Original artwork: no logos and no names of makers.

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "sas/amp_def.h"

class AmpFace final : public juce::Component {
public:
    AmpFace(juce::AudioProcessorValueTreeState& apvts, const sas::AmpDef& def);
    ~AmpFace() override;
    // True when `def` asks for a faceplate and this build carries its images.
    static bool available(const sas::AmpDef& def);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    class KnobLook;
    struct Knob {
        juce::Slider slider{juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox};
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    juce::Rectangle<float> artBounds() const;                    // where the artwork lands in this component
    juce::Point<float> toScreen(std::pair<double, double> p) const;
    int jackAt(juce::Point<int> p) const;

    juce::AudioProcessorValueTreeState& apvts_;
    sas::FaceplateDef plate_;
    juce::Image background_, knob_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::unique_ptr<KnobLook> look_;
};
