#include "RackPage.h"

using sas::ModuleType;

namespace {

struct Category {
    const char* name;
    std::vector<ModuleType> types;
};

const std::vector<Category>& categories() {
    static const std::vector<Category> c{
        {"Effects", {ModuleType::Gate, ModuleType::Compressor, ModuleType::Overdrive, ModuleType::NamPedal}},
        {"Modulation", {ModuleType::Phaser, ModuleType::Chorus, ModuleType::Vibe}},
        {"Pitch", {ModuleType::Pitch}},
        {"EQ", {ModuleType::Eq}},
        {"Space", {ModuleType::Delay, ModuleType::Reverb}},
        {"Cabinet", {ModuleType::Cabinet}},
    };
    return c;
}

bool sameOrder(const sas::RackOrder& a, const sas::RackOrder& b) {
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i)
        if (!(a.slots[std::size_t(i)] == b.slots[std::size_t(i)])) return false;
    return true;
}

const AmpLookAndFeel* lookOf(const juce::Component& c) { return dynamic_cast<const AmpLookAndFeel*>(&c.getLookAndFeel()); }

}  // namespace

// ------------------------------------------------------------------ signal map

void RackPage::Map::paint(juce::Graphics& g) {
    const auto* look = lookOf(*this);
    const auto accent = look ? look->accent() : juce::Colour(0xffd9a441);
    const auto text = look ? look->text() : juce::Colours::white;
    const auto panel = look ? look->panel() : juce::Colour(0xff2a2723);
    boxes_.clear();
    g.setFont(AmpLookAndFeel::font(11.0f, true, 0.06f));
    int x = 4;
    const int y = 5, h = getHeight() - 10;
    for (std::size_t i = 0; i < chips.size(); ++i) {
        const int w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), chips[i].first) + 18;
        if (x + w > getWidth()) break;
        const juce::Rectangle<int> box(x, y, w, h);
        boxes_.push_back(box);
        g.setColour(chips[i].second ? accent.withAlpha(0.30f) : panel.brighter(0.12f));
        g.fillRoundedRectangle(box.toFloat(), 5.0f);
        g.setColour(chips[i].second ? accent : text.withAlpha(0.35f));
        g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 5.0f, 1.0f);
        g.setColour(chips[i].second ? accent : text.withAlpha(0.85f));
        g.drawText(chips[i].first, box, juce::Justification::centred);
        x += w;
        if (i + 1 < chips.size()) {
            g.setColour(text.withAlpha(0.4f));
            g.drawText(">", x, y, 12, h, juce::Justification::centred);
            x += 12;
        }
    }
}

void RackPage::Map::mouseUp(const juce::MouseEvent& e) {
    for (std::size_t i = 0; i < boxes_.size(); ++i)
        if (boxes_[i].contains(e.getPosition()) && onChip) onChip(int(i));
}

// ------------------------------------------------------------------ page

RackPage::RackPage(SasProcessor& processor) : processor_(processor) {
    addAndMakeVisible(map_);
    addAndMakeVisible(add_);
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    map_.onChip = [this](int i) {
        if (i >= 0 && i < unitCount()) viewport_.setViewPosition(0, units_[std::size_t(i)]->getY());
    };
    add_.setTooltip("Add a module at the end of the rack");
    add_.onClick = [this] { showAddMenu(-1, add_); };
    rebuild();
    startTimerHz(6);
}

RackPage::~RackPage() { stopTimer(); }

void RackPage::rebuild() {
    shown_ = processor_.rackOrder();
    units_.clear();
    list_.removeAllChildren();
    map_.chips.clear();
    for (int i = 0; i < shown_.count; ++i) {
        const sas::SlotRef slot = shown_.slots[std::size_t(i)];
        auto unit = std::make_unique<ModuleUnit>(processor_, slot);
        unit->onRemove = [this](sas::SlotRef s) {
            processor_.removeModule(s);
            rebuild();
        };
        unit->onMove = [this](sas::SlotRef s, int delta) {
            for (int k = 0; k < shown_.count; ++k)
                if (shown_.slots[std::size_t(k)] == s) {
                    processor_.moveModule(s, k + delta);
                    break;
                }
            rebuild();
        };
        unit->onAddAfter = [this, i](sas::SlotRef) { showAddMenu(i + 1, *units_[std::size_t(i)]); };
        list_.addAndMakeVisible(*unit);
        map_.chips.push_back({ModuleUnit::title(slot), slot.type == ModuleType::Amp});
        units_.push_back(std::move(unit));
    }
    map_.repaint();
    layoutUnits();
}

void RackPage::layoutUnits() {
    const int width = std::max(100, viewport_.getMaximumVisibleWidth());
    int y = 0;
    for (auto& u : units_) {
        const int h = u->preferredHeight();
        u->setBounds(0, y, width, h);
        y += h + 8;
    }
    list_.setSize(width, std::max(y, 1));
}

void RackPage::resized() {
    auto r = getLocalBounds();
    auto top = r.removeFromTop(34);
    add_.setBounds(top.removeFromRight(110).reduced(0, 3));
    map_.setBounds(top);
    r.removeFromTop(4);
    viewport_.setBounds(r);
    layoutUnits();
}

void RackPage::paint(juce::Graphics& g) {
    if (dropLine_ < 0) return;
    const auto* look = lookOf(*this);
    g.setColour(look ? look->accent() : juce::Colour(0xffd9a441));
    const int y = viewport_.getY() + dropLine_ - viewport_.getViewPositionY();
    g.fillRect(viewport_.getX(), y - 1, viewport_.getMaximumVisibleWidth(), 3);
}

void RackPage::timerCallback() {
    if (!sameOrder(shown_, processor_.rackOrder())) {
        rebuild();
        return;
    }
    for (auto& u : units_) u->refresh();
}

juce::PopupMenu RackPage::addMenu(const SasProcessor& processor, int idBase) {
    juce::PopupMenu menu;
    for (const auto& cat : categories()) {
        juce::PopupMenu sub;
        for (auto t : cat.types) {
            const bool ok = processor.canAdd(t);
            sub.addItem(idBase + int(t), ModuleUnit::title({t, 0}) + (ok ? juce::String() : "  (limit reached)"), ok);
        }
        menu.addSubMenu(cat.name, sub);
    }
    return menu;
}

void RackPage::showAddMenu(int insertAt, juce::Component& anchor) {
    constexpr int kBase = 100;
    addMenu(processor_, kBase).showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [this, insertAt](int choice) {
        if (choice < kBase) return;
        processor_.addModule(static_cast<ModuleType>(choice - kBase), insertAt);
        rebuild();
    });
}

int RackPage::dropIndexAt(juce::Point<int> pagePoint, sas::SlotRef dragged) const {
    const int y = list_.getLocalPoint(this, pagePoint).y;
    int index = 0;
    for (const auto& u : units_) {
        if (u->slot() == dragged) continue;
        if (u->getY() + u->getHeight() / 2 < y) ++index;
    }
    return index;
}

bool RackPage::isInterestedInDragSource(const SourceDetails& d) {
    sas::SlotRef slot;
    return ModuleUnit::parseDragDescription(d.description.toString(), slot);
}

void RackPage::itemDragMove(const SourceDetails& d) {
    sas::SlotRef slot;
    if (!ModuleUnit::parseDragDescription(d.description.toString(), slot)) return;
    const int index = dropIndexAt(d.localPosition, slot);
    // The line goes above the unit that would follow the dragged one.
    int seen = 0, y = list_.getHeight();
    for (const auto& u : units_) {
        if (u->slot() == slot) continue;
        if (seen == index) {
            y = u->getY() - 4;
            break;
        }
        ++seen;
    }
    if (y != dropLine_) {
        dropLine_ = y;
        repaint();
    }
}

void RackPage::itemDragExit(const SourceDetails&) {
    dropLine_ = -1;
    repaint();
}

void RackPage::itemDropped(const SourceDetails& d) {
    dropLine_ = -1;
    sas::SlotRef slot;
    if (ModuleUnit::parseDragDescription(d.description.toString(), slot)) {
        processor_.moveModule(slot, dropIndexAt(d.localPosition, slot));
        rebuild();
    }
    repaint();
}
