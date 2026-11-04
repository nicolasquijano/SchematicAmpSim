#pragma once
// The rack page (docs/MODULAR_RACK.md, section 7): the signal path as a grid of tiles, in order, with a "+" at the end; below it one panel with the
// controls of the selected module. A tile shows the module's name, whether it is on (click the light to bypass it) and what it costs; drag a tile
// to move it, click it to open it. With a parallel the path splits in two rows between the splitter and the mixer: lane A above, lane B below.

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
    bool unitShown(int index) const { return units_[std::size_t(index)]->isVisible(); }
    int tileCount() const { return int(map_.tiles.size()); }
    int tileLane(int index) const { return map_.tiles[std::size_t(index)].lane; }
    sas::SlotRef selectedSlot() const { return selected_; }
    void select(sas::SlotRef slot);
    bool unitSumsToMono(int index) const { return units_[std::size_t(index)]->sumsToMono(); }
    // Builds the units again from the processor's order (done by itself when the order changes).
    void rebuild();
    // The panel of the selected module changed height (the amp with a faceplate is taller than one with knobs).
    void relayout() { layoutUnits(); }
    // The "add module" menu, by category; an item's id is `idBase` plus the module type (`idBase - 1` is the parallel), and it is greyed out when that
    // type cannot be added.
    static juce::PopupMenu addMenu(const SasProcessor& processor, int idBase);
    void showAddMenu(int insertAt, juce::Component& anchor);
    // Total CPU of the modules of the rack, in percent of the time of a block.
    float totalCpu() const;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    // The grid of tiles.
    class Map final : public juce::Component {
    public:
        struct Tile {
            sas::SlotRef slot;
            juce::String name;
            int lane = 0;  // 0 shared or lane A, 1 lane B
            bool hasSwitch = false, on = true;
            float cpu = 0.0f;
        };
        struct Layout {
            std::vector<juce::Rectangle<int>> boxes;
            juce::Rectangle<int> plus;
            int height = 0;
        };
        static constexpr int kTileW = 118, kTileH = 66, kGap = 12;

        Layout layout(int width) const;
        int heightFor(int width) const { return layout(width).height; }
        juce::Rectangle<int> boxOf(int index) const;  // index == tiles.size() is the "+" tile
        int tileAt(juce::Point<int> p) const;         // -1 when none; tiles.size() for the "+" tile
        // Where a dragged tile would land: the index among the other tiles, and the lane when that is between the splitter and the mixer (-1 otherwise).
        struct Drop {
            int index = 0, lane = -1;
        };
        Drop dropAt(juce::Point<int> p, sas::SlotRef dragged) const;
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;

        std::vector<Tile> tiles;
        int selected = -1;
        int marker = -1;  // a drop marker while dragging: the tile it goes in front of
        std::function<void(int)> onSelect, onToggle;
        std::function<void()> onAdd;

    private:
        int pressed_ = -1;
        bool pressedLight_ = false;
    };

    void timerCallback() override;
    void layoutUnits();
    void refreshTiles();
    void removeParallelAsked();
    ModuleUnit* unitFor(sas::SlotRef slot) const;

    SasProcessor& processor_;
    ModuleUnit::HostedBodyFactory hosted_;
    Map map_;
    juce::Viewport viewport_;
    juce::Component list_;
    std::vector<std::unique_ptr<ModuleUnit>> units_;
    sas::RackOrder shown_;
    sas::SlotRef selected_{sas::ModuleType::Amp, 0};
    int ticks_ = 0;
};
