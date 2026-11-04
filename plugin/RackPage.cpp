#include "RackPage.h"

#include "RackState.h"

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

// One colour per family of modules, so the path reads at a glance.
juce::Colour colourOf(ModuleType t) {
    switch (t) {
        case ModuleType::Amp: return juce::Colour(0xffe0a030);
        case ModuleType::Cabinet: return juce::Colour(0xffb07a48);
        case ModuleType::Gate:
        case ModuleType::Compressor: return juce::Colour(0xff58b878);
        case ModuleType::Overdrive:
        case ModuleType::NamPedal: return juce::Colour(0xffe06a3c);
        case ModuleType::Phaser:
        case ModuleType::Chorus:
        case ModuleType::Vibe: return juce::Colour(0xffa070d8);
        case ModuleType::Pitch: return juce::Colour(0xffd868a8);
        case ModuleType::Eq: return juce::Colour(0xff48b8c8);
        case ModuleType::Delay:
        case ModuleType::Reverb: return juce::Colour(0xff5890e0);
    }
    return juce::Colours::grey;
}

}  // namespace

// ------------------------------------------------------------------ the grid of tiles

int RackPage::Map::heightFor(int width) const {
    const int cols = std::max(1, (width + kGap) / (kTileW + kGap));
    const int rows = (int(tiles.size()) + 1 + cols - 1) / cols;  // the "+" tile too
    return rows * (kTileH + kGap);
}

juce::Rectangle<int> RackPage::Map::boxOf(int index) const {
    const int cols = columns();
    return {(index % cols) * (kTileW + kGap), (index / cols) * (kTileH + kGap), kTileW, kTileH};
}

int RackPage::Map::tileAt(juce::Point<int> p) const {
    for (int i = 0; i <= int(tiles.size()); ++i)
        if (boxOf(i).contains(p)) return i;
    return -1;
}

int RackPage::Map::dropIndex(juce::Point<int> p, sas::SlotRef dragged) const {
    // The nearest tile centre, reading order: dropping on a tile puts the dragged one in its place.
    int best = 0;
    float bestD = 1e9f;
    int index = 0;
    for (const auto& t : tiles) {
        if (t.slot == dragged) continue;
        const float d = boxOf(int(&t - tiles.data())).getCentre().toFloat().getDistanceFrom(p.toFloat());
        if (d < bestD) {
            bestD = d;
            best = index;
        }
        ++index;
    }
    // Past the middle of that tile (along the row), it goes after it.
    int k = 0;
    for (const auto& t : tiles) {
        if (t.slot == dragged) continue;
        if (k == best) {
            const auto box = boxOf(int(&t - tiles.data()));
            return p.x > box.getCentreX() ? best + 1 : best;
        }
        ++k;
    }
    return best;
}

void RackPage::Map::paint(juce::Graphics& g) {
    const auto* look = lookOf(*this);
    const auto accent = look ? look->accent() : juce::Colour(0xffd9a441);
    const auto text = look ? look->text() : juce::Colours::white;
    const auto panel = look ? look->panel() : juce::Colour(0xff2a2723);
    const int cols = columns();
    for (int i = 0; i < int(tiles.size()); ++i) {
        const auto& t = tiles[std::size_t(i)];
        const auto box = boxOf(i);
        const auto family = colourOf(t.slot.type);
        const bool lit = !t.hasSwitch || t.on;
        const bool sel = i == selected;
        // The wire to the next tile of the row.
        if (i + 1 < int(tiles.size()) && (i % cols) + 1 < cols) {
            g.setColour(text.withAlpha(0.35f));
            g.fillRect(box.getRight(), box.getCentreY() - 1, kGap, 2);
        }
        const auto f = box.toFloat();
        g.setGradientFill(juce::ColourGradient(panel.brighter(lit ? 0.22f : 0.08f), 0.0f, f.getY(), panel.darker(0.2f), 0.0f, f.getBottom(), false));
        g.fillRoundedRectangle(f, 9.0f);
        g.setColour(family.withAlpha(lit ? 0.95f : 0.30f));
        g.fillRoundedRectangle(f.getX() + 8.0f, f.getY() + 6.0f, f.getWidth() - 16.0f, 4.0f, 2.0f);
        g.setColour(sel ? accent : text.withAlpha(0.22f));
        g.drawRoundedRectangle(f.reduced(0.5f), 9.0f, sel ? 2.5f : 1.0f);
        g.setColour(text.withAlpha(lit ? 0.95f : 0.45f));
        g.setFont(AmpLookAndFeel::font(12.5f, true, 0.04f));
        g.drawFittedText(t.name, box.withTrimmedTop(14).withTrimmedBottom(22).reduced(6, 0), juce::Justification::centred, 2, 0.8f);
        if (t.hasSwitch) {  // the bypass light: click it
            const auto dot = juce::Rectangle<float>(f.getX() + 10.0f, f.getBottom() - 19.0f, 11.0f, 11.0f);
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.fillEllipse(dot.expanded(2.0f));
            g.setColour(t.on ? juce::Colour(0xff7be08a) : text.withAlpha(0.25f));
            g.fillEllipse(dot);
        }
        if (t.cpu > 0.05f) {
            g.setColour(text.withAlpha(0.55f));
            g.setFont(AmpLookAndFeel::font(10.0f));
            g.drawText(juce::String(t.cpu, 1) + "%", box.withTrimmedRight(8).removeFromBottom(22), juce::Justification::centredRight);
        }
    }
    // The "+" tile.
    {
        const auto box = boxOf(int(tiles.size()));
        g.setColour(text.withAlpha(0.35f));
        g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 9.0f, 1.0f);
        g.setColour(text.withAlpha(0.7f));
        g.setFont(AmpLookAndFeel::font(26.0f, true));
        g.drawText("+", box, juce::Justification::centred);
    }
    if (dropAt >= 0) {  // where a dragged tile would land
        const auto box = boxOf(dropAt);
        g.setColour(accent);
        g.fillRect(box.getX() - kGap / 2 - 1, box.getY() + 4, 3, box.getHeight() - 8);
    }
}

void RackPage::Map::mouseDown(const juce::MouseEvent& e) {
    pressed_ = tileAt(e.getPosition());
    pressedLight_ = false;
    if (pressed_ >= 0 && pressed_ < int(tiles.size()) && tiles[std::size_t(pressed_)].hasSwitch) {
        const auto box = boxOf(pressed_);
        pressedLight_ = juce::Rectangle<int>(box.getX(), box.getBottom() - 28, 32, 28).contains(e.getPosition());
    }
}

void RackPage::Map::mouseDrag(const juce::MouseEvent& e) {
    if (pressed_ < 0 || pressed_ >= int(tiles.size()) || pressedLight_ || e.getDistanceFromDragStart() < 8) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        if (!container->isDragAndDropActive()) container->startDragging(ModuleUnit::dragDescription(tiles[std::size_t(pressed_)].slot), this);
}

void RackPage::Map::mouseUp(const juce::MouseEvent& e) {
    const int at = tileAt(e.getPosition());
    if (at >= 0 && at == pressed_ && e.getDistanceFromDragStart() < 8) {
        if (at == int(tiles.size())) {
            if (onAdd) onAdd();
        } else if (pressedLight_) {
            if (onToggle) onToggle(at);
        } else if (onSelect) {
            onSelect(at);
        }
    }
    pressed_ = -1;
}

// ------------------------------------------------------------------ page

RackPage::RackPage(SasProcessor& processor, ModuleUnit::HostedBodyFactory hosted) : processor_(processor), hosted_(std::move(hosted)) {
    addAndMakeVisible(map_);
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&list_, false);
    viewport_.setScrollBarsShown(true, false);
    map_.onSelect = [this](int i) {
        if (i >= 0 && i < unitCount()) select(unitSlot(i));
    };
    map_.onToggle = [this](int i) {
        if (i < 0 || i >= unitCount()) return;
        const auto slot = unitSlot(i);
        if (const char* on = RackState::onId(slot.type))
            if (auto* p = processor_.apvts.getParameter(RackState::paramId(on, slot.instance))) {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f);
                p->endChangeGesture();
            }
        refreshTiles();
    };
    map_.onAdd = [this] { showAddMenu(-1, map_); };
    rebuild();
    startTimerHz(20);
}

RackPage::~RackPage() { stopTimer(); }

ModuleUnit* RackPage::unitFor(sas::SlotRef slot) const {
    for (const auto& u : units_)
        if (u->slot() == slot) return u.get();
    return nullptr;
}

void RackPage::select(sas::SlotRef slot) {
    if (!unitFor(slot)) return;
    selected_ = slot;
    map_.selected = -1;
    for (int i = 0; i < unitCount(); ++i)
        if (unitSlot(i) == slot) map_.selected = i;
    layoutUnits();
    map_.repaint();
}

void RackPage::rebuild() {
    const auto before = shown_;
    shown_ = processor_.rackOrder();
    units_.clear();
    list_.removeAllChildren();
    map_.tiles.clear();
    for (int i = 0; i < shown_.count; ++i) {
        const sas::SlotRef slot = shown_.slots[std::size_t(i)];
        auto unit = std::make_unique<ModuleUnit>(processor_, slot, hosted_);
        unit->onRemove = [this](sas::SlotRef s) {
            int at = 0;
            for (int k = 0; k < shown_.count; ++k)
                if (shown_.slots[std::size_t(k)] == s) at = k;
            processor_.removeModule(s);
            rebuild();
            select(unitSlot(std::max(0, std::min(at - 1, unitCount() - 1))));  // the one before it
        };
        unit->onMove = [this](sas::SlotRef s, int delta) {
            for (int k = 0; k < shown_.count; ++k)
                if (shown_.slots[std::size_t(k)] == s) {
                    processor_.moveModule(s, k + delta);
                    break;
                }
            rebuild();
            select(s);
        };
        unit->onAddAfter = [this, i](sas::SlotRef) { showAddMenu(i + 1, map_); };
        list_.addChildComponent(*unit);
        Map::Tile tile;
        tile.slot = slot;
        tile.name = ModuleUnit::title(slot);
        tile.hasSwitch = RackState::onId(slot.type) != nullptr;
        map_.tiles.push_back(tile);
        units_.push_back(std::move(unit));
    }
    // Where a mono module follows a stereo one, the pair is summed: the unit says so.
    bool stereo = false;
    for (int i = 0; i < shown_.count; ++i) {
        const auto channels = sas::moduleChannels(shown_.slots[std::size_t(i)].type);
        units_[std::size_t(i)]->setSumsToMono(stereo && (channels == sas::Channels::MonoOnly || channels == sas::Channels::MonoToStereo));
        if (channels == sas::Channels::MonoOnly) stereo = false;
        else if (channels != sas::Channels::Flexible) stereo = true;
    }
    // A module that is new takes the selection; otherwise the one that was selected keeps it (the amp, at the start).
    sas::SlotRef pick = unitFor(selected_) ? selected_ : sas::SlotRef{ModuleType::Amp, 0};
    if (before.count > 0 && shown_.count == before.count + 1)
        for (int i = 0; i < shown_.count; ++i) {
            bool had = false;
            for (int k = 0; k < before.count; ++k) had = had || before.slots[std::size_t(k)] == shown_.slots[std::size_t(i)];
            if (!had) pick = shown_.slots[std::size_t(i)];
        }
    refreshTiles();
    selected_ = pick;
    select(unitFor(pick) ? pick : unitSlot(0));
}

void RackPage::refreshTiles() {
    for (auto& t : map_.tiles) {
        if (const char* on = RackState::onId(t.slot.type)) {
            if (auto* v = processor_.apvts.getRawParameterValue(RackState::paramId(on, t.slot.instance))) t.on = v->load() > 0.5f;
        }
        t.cpu = processor_.rack().moduleCpu(t.slot);
    }
    map_.repaint();
}

void RackPage::layoutUnits() {
    const int width = std::max(100, getWidth());
    const int mapH = map_.heightFor(width);
    map_.setBounds(0, 0, width, mapH);
    viewport_.setBounds(0, mapH + 4, width, std::max(0, getHeight() - mapH - 4));
    const int w = std::max(100, viewport_.getMaximumVisibleWidth());
    int h = 1;
    for (auto& u : units_) {
        const bool show = u->slot() == selected_;
        u->setVisible(show);
        if (show) {
            h = u->preferredHeight();
            u->setBounds(0, 0, w, h);
        }
    }
    list_.setSize(w, h);
}

void RackPage::resized() { layoutUnits(); }

void RackPage::paint(juce::Graphics&) {}

void RackPage::timerCallback() {
    if (!sameOrder(shown_, processor_.rackOrder())) {
        rebuild();
        return;
    }
    for (auto& u : units_) u->tick();
    refreshTiles();
    if (++ticks_ % 4 == 0)
        for (auto& u : units_) u->refresh();  // the names of loaded models, at about 5 Hz
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

bool RackPage::isInterestedInDragSource(const SourceDetails& d) {
    sas::SlotRef slot;
    return ModuleUnit::parseDragDescription(d.description.toString(), slot);
}

void RackPage::itemDragMove(const SourceDetails& d) {
    sas::SlotRef slot;
    if (!ModuleUnit::parseDragDescription(d.description.toString(), slot)) return;
    // The marker sits in front of the tile that would follow the dragged one.
    const int index = map_.dropIndex(map_.getLocalPoint(this, d.localPosition), slot);
    int seen = 0, marker = int(map_.tiles.size());
    for (int i = 0; i < int(map_.tiles.size()); ++i) {
        if (map_.tiles[std::size_t(i)].slot == slot) continue;
        if (seen == index) {
            marker = i;
            break;
        }
        ++seen;
    }
    if (marker != map_.dropAt) {
        map_.dropAt = marker;
        map_.repaint();
    }
}

void RackPage::itemDragExit(const SourceDetails&) {
    map_.dropAt = -1;
    map_.repaint();
}

void RackPage::itemDropped(const SourceDetails& d) {
    map_.dropAt = -1;
    sas::SlotRef slot;
    if (ModuleUnit::parseDragDescription(d.description.toString(), slot)) {
        processor_.moveModule(slot, map_.dropIndex(map_.getLocalPoint(this, d.localPosition), slot));
        rebuild();
        select(slot);
    }
    map_.repaint();
}
