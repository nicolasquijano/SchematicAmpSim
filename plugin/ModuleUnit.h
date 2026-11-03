#pragma once
// One unit of the rack page: the header (drag handle, name, On switch, move up / down, remove, add after) and the controls of one module instance
// (docs/MODULAR_RACK.md, section 7). The controls are bound to the parameters of the instance, so two overdrives each have their own knobs.

#include <functional>
#include <memory>

#include "PluginEditor.h"
#include "sas/rack.h"

class ModuleUnit final : public juce::Component {
public:
    static constexpr int kHeaderHeight = 30;

    // What a unit shows under its header (the controls of the module).
    class Body : public juce::Component {
    public:
        virtual int height() const = 0;
    };

    ModuleUnit(SasProcessor& processor, sas::SlotRef slot);
    ~ModuleUnit() override;

    sas::SlotRef slot() const { return slot_; }
    int preferredHeight() const;
    // "OVERDRIVE", "OVERDRIVE 2"...
    static juce::String title(sas::SlotRef slot);
    // The description a drag of this unit carries, and its reading back.
    static juce::String dragDescription(sas::SlotRef slot);
    static bool parseDragDescription(const juce::String& text, sas::SlotRef& slot);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void refresh();  // the names of loaded models (the page's timer calls it)
    void setCpu(float percent);

    std::function<void(sas::SlotRef)> onRemove, onAddAfter;
    std::function<void(sas::SlotRef, int)> onMove;  // -1 up, +1 down

private:
    SasProcessor& processor_;
    sas::SlotRef slot_;
    juce::ToggleButton on_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt_;
    juce::TextButton up_{"^"}, down_{"v"}, remove_{"x"}, add_{"+"};
    juce::TextButton extraA_, extraB_;  // Tap / Presets / Load... / Clear, depending on the module
    juce::Label name_;
    std::unique_ptr<Body> body_;
    std::unique_ptr<juce::FileChooser> chooser_;
    double lastTapMs_ = 0.0;
    float cpu_ = 0.0f;
};
