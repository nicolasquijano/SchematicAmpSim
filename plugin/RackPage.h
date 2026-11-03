#pragma once
// The rack page: a signal map on top, the modules below it in signal order (a vertical list of units that scrolls), a "+" to add modules and
// drag and drop (or the arrows of each unit) to reorder them (docs/MODULAR_RACK.md, section 7).

#include <memory>
#include <vector>

#include "ModuleUnit.h"

class RackPage final : public juce::Component, public juce::DragAndDropTarget, private juce::Timer {
public:
    explicit RackPage(SasProcessor& processor, ModuleUnit::HostedBodyFactory hosted = {});
    ~RackPage() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    int unitCount() const { return int(units_.size()); }
    sas::SlotRef unitSlot(int index) const { return units_[std::size_t(index)]->slot(); }
    // Builds the units again from the processor's order (done by itself when the order changes).
    void rebuild();
    // The units took another height (the amp with a faceplate is taller than one with knobs).
    void relayout() { layoutUnits(); }
    bool unitSumsToMono(int index) const { return units_[std::size_t(index)]->sumsToMono(); }
    // The "add module" menu, by category; an item's id is `idBase` plus the module type, and it is greyed out when that type cannot be added.
    static juce::PopupMenu addMenu(const SasProcessor& processor, int idBase);
    void showAddMenu(int insertAt, juce::Component& anchor);

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    // The chips of the signal map: the whole path at a glance, the amp highlighted; a click brings the unit into view.
    class Map final : public juce::Component {
    public:
        void paint(juce::Graphics&) override;
        void mouseUp(const juce::MouseEvent&) override;
        std::vector<std::pair<juce::String, bool>> chips;  // name, is the amp
        std::function<void(int)> onChip;

    private:
        std::vector<juce::Rectangle<int>> boxes_;
    };

    void timerCallback() override;
    void layoutUnits();
    int dropIndexAt(juce::Point<int> pagePoint, sas::SlotRef dragged) const;

    SasProcessor& processor_;
    ModuleUnit::HostedBodyFactory hosted_;
    Map map_;
    juce::TextButton add_{"+ Add module"};
    juce::Viewport viewport_;
    juce::Component list_;
    std::vector<std::unique_ptr<ModuleUnit>> units_;
    sas::RackOrder shown_;
    int ticks_ = 0;
    int dropLine_ = -1;  // y in list_ of the insertion line while dragging, -1 when none
};
