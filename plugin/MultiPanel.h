#pragma once
// The multi impulse loader's panel (cabinet mode MULTI): a plane where a cursor mixes eight impulses, and a strip for each impulse.

#include <memory>

#include "PluginEditor.h"

// The plane: the eight impulses are points on it (drag one to move it, double click to put it back), the cursor is the ring (drag anywhere else).
// The closer the cursor, the more of that impulse is heard; the glow of a point shows how much.
class MultiPad final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit MultiPad(SasProcessor& p);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> plane() const { return getLocalBounds().toFloat().reduced(8.0f); }
    juce::Point<float> toPixel(float x, float y) const;
    void setParam(const juce::String& id, float value);
    void gesture(const juce::String& id, bool begin);
    int nodeAt(juce::Point<float> p) const;

    SasProcessor& processor_;
    int dragNode_ = -1;
    bool dragCursor_ = false;
};

class MultiPanel final : public juce::Component, private juce::Timer {
public:
    explicit MultiPanel(SasProcessor& p);
    ~MultiPanel() override;
    static constexpr int kHeight = 250;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

private:
    struct Strip {
        juce::Label name, note;
        juce::TextButton load{"Load..."}, clear{"x"}, solo{"S"}, polarity{"Auto"};  // polarity: a click goes Auto, +, -, Auto...
        juce::Slider level, delay, hp, lp;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> levelAtt, delayAtt, hpAtt, lpAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloAtt;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void showLoadMenu(int slot, juce::Component& anchor);
    void cyclePolarity(int slot);
    void chooseFile(int slot);
    void styleBar(juce::Slider& s);

    SasProcessor& processor_;
    MultiPad pad_;
    Strip strips_[SasProcessor::kMultiSlots];
    std::vector<PanelBox> boxes_;
    std::unique_ptr<juce::FileChooser> chooser_;
};
