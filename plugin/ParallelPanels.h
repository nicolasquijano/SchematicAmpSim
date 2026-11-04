#pragma once
// The controls of the second amp and the second cabinet of a parallel (lane B): the same things as the first ones, bound to the parameters of instance 1.
// (The first amp's and cabinet's controls live in SasEditor, where they always were; docs/superpowers/specs/2026-11-03-parallel-paths-design.md.)

#include <memory>
#include <vector>

#include "PluginEditor.h"

class AmpPanelB final : public juce::Component, private juce::Timer {
public:
    static constexpr int kInstance = 1;

    explicit AmpPanelB(SasProcessor& processor);
    ~AmpPanelB() override { stopTimer(); }

    int preferredHeight() const { return 380; }
    void resized() override;
    void paint(juce::Graphics& g) override {
        if (const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel())) paintPanelBoxes(g, *look, panels_);
    }

private:
    void timerCallback() override;
    void rebuildForAmp();  // the knobs, jacks, switches and tubes of the amp that is chosen

    SasProcessor& processor_;
    int shownAmp_ = -1;
    std::vector<PanelBox> panels_;
    juce::ComboBox ampBox_, quality_, jack_, pickup_, tube_, noise_, unit_, selBox_[4];
    juce::Label ampCaption_, qualityCaption_, jackCaption_, pickupCaption_, tubeCaption_, noiseCaption_, unitCaption_, selCaption_[4], note_;
    std::vector<std::unique_ptr<KnobWithLabel>> knobs_;
    KnobWithLabel input_, cable_, bias_, wear_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampAtt_, qualityAtt_, pickupAtt_, noiseAtt_, unitAtt_;
};

class CabinetPanelB final : public juce::Component, private juce::Timer {
public:
    static constexpr int kInstance = 1;

    explicit CabinetPanelB(SasProcessor& processor);
    ~CabinetPanelB() override { stopTimer(); }

    int preferredHeight() const { return 300; }
    void resized() override;
    void paint(juce::Graphics& g) override {
        if (const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel())) paintPanelBoxes(g, *look, panels_);
    }

private:
    void timerCallback() override;
    void showImpulseMenu(int slot, juce::Component& anchor);

    SasProcessor& processor_;
    std::vector<PanelBox> panels_;
    juce::ComboBox cab_;
    juce::Label cabCaption_, nameA_, nameB_, note_;
    juce::TextButton loadA_{"Load..."}, clearA_{"Clear"}, loadB_{"Load..."}, clearB_{"Clear"};
    IrCurve curveA_, curveB_;
    KnobWithLabel blend_, width_, doubleAmount_, doubleTime_, speaker_;  // the speaker's dynamics belong to the second amp, and are with its cabinet
    juce::ToggleButton intense_{"Intense"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cabAtt_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> intenseAtt_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
