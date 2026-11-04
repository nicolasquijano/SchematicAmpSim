#include "RackPage.h"

#include "RackState.h"

using sas::ModuleType;

namespace {

constexpr int kLaneBOffset = 50;  // the items of the lane B menu come this far after the module types

struct Category {
    const char* name;
    std::vector<ModuleType> types;
};

const std::vector<Category>& categories() {
    static const std::vector<Category> c{
        {"Effects", {ModuleType::Gate, ModuleType::Compressor, ModuleType::Overdrive, ModuleType::NamPedal}},
        {"Modulation", {ModuleType::Phaser, ModuleType::Chorus, ModuleType::Vibe, ModuleType::Flanger, ModuleType::Tremolo}},
        {"Pitch", {ModuleType::Pitch}},
        {"EQ", {ModuleType::Eq}},
        {"Space", {ModuleType::Delay, ModuleType::Reverb}},
    };
    return c;
}

bool sameOrder(const sas::RackOrder& a, const sas::RackOrder& b) {
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i)
        if (!(a.slots[std::size_t(i)] == b.slots[std::size_t(i)]) || a.lane[std::size_t(i)] != b.lane[std::size_t(i)]) return false;
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
        case ModuleType::Vibe:
        case ModuleType::Tremolo:
        case ModuleType::Flanger: return juce::Colour(0xffa070d8);
        case ModuleType::Pitch: return juce::Colour(0xffd868a8);
        case ModuleType::Eq: return juce::Colour(0xff48b8c8);
        case ModuleType::Delay:
        case ModuleType::Reverb: return juce::Colour(0xff5890e0);
        case ModuleType::Splitter:
        case ModuleType::Mixer: return juce::Colour(0xffc8c8c8);
    }
    return juce::Colours::grey;
}

}  // namespace

// ------------------------------------------------------------------ the grid of tiles

RackPage::Map::Layout RackPage::Map::layout(int width) const {
    Layout out;
    const int n = int(tiles.size());
    int s = -1, m = -1;
    for (int i = 0; i < n; ++i) {
        if (tiles[std::size_t(i)].slot.type == ModuleType::Splitter) s = i;
        if (tiles[std::size_t(i)].slot.type == ModuleType::Mixer) m = i;
    }
    out.boxes.resize(std::size_t(n));
    if (s < 0 || m < 0) {  // one chain: a grid that wraps
        const int cols = std::max(1, (width + kGap) / (kTileW + kGap));
        auto box = [&](int index) { return juce::Rectangle<int>((index % cols) * (kTileW + kGap), (index / cols) * (kTileH + kGap), kTileW, kTileH); };
        for (int i = 0; i < n; ++i) out.boxes[std::size_t(i)] = box(i);
        out.plus = box(n);
        out.height = ((n + 1 + cols - 1) / cols) * (kTileH + kGap);
        return out;
    }
    // A parallel: two rows. Before the splitter and after the mixer the path is in the upper row; between them lane A is above lane B.
    int nA = 0, nB = 0;
    for (int i = s + 1; i < m; ++i) (tiles[std::size_t(i)].lane == 1 ? nB : nA)++;
    const int cols = (s + 1) + std::max(nA, nB) + (n - m) + 1;
    const int tileW = std::clamp((width + kGap) / std::max(1, cols) - kGap, 72, kTileW);
    auto box = [&](int row, int col) { return juce::Rectangle<int>(col * (tileW + kGap), row * (kTileH + kGap), tileW, kTileH); };
    int col = 0;
    for (int i = 0; i <= s; ++i) out.boxes[std::size_t(i)] = box(0, col++);
    int colA = col, colB = col;
    for (int i = s + 1; i < m; ++i) out.boxes[std::size_t(i)] = tiles[std::size_t(i)].lane == 1 ? box(1, colB++) : box(0, colA++);
    col = (s + 1) + std::max(nA, nB);
    const int mixerCol = col;
    for (int i = m; i < n; ++i) out.boxes[std::size_t(i)] = box(0, col++);
    out.plus = box(1, mixerCol + 1);  // not under the mixer, where its wire comes down
    out.height = 2 * (kTileH + kGap);
    return out;
}

juce::Rectangle<int> RackPage::Map::boxOf(int index) const {
    const auto l = layout(getWidth());
    return index >= int(l.boxes.size()) ? l.plus : l.boxes[std::size_t(index)];
}

int RackPage::Map::tileAt(juce::Point<int> p) const {
    const auto l = layout(getWidth());
    for (int i = 0; i < int(l.boxes.size()); ++i)
        if (l.boxes[std::size_t(i)].contains(p)) return i;
    return l.plus.contains(p) ? int(tiles.size()) : -1;
}

RackPage::Map::Drop RackPage::Map::dropAt(juce::Point<int> p, sas::SlotRef dragged) const {
    const auto l = layout(getWidth());
    const int n = int(tiles.size());
    int s = -1, m = -1;
    for (int i = 0; i < n; ++i) {
        if (tiles[std::size_t(i)].slot.type == ModuleType::Splitter) s = i;
        if (tiles[std::size_t(i)].slot.type == ModuleType::Mixer) m = i;
    }
    // Index among the other tiles of an index among all of them.
    auto among = [&](int at) {
        int k = 0;
        for (int i = 0; i < at && i < n; ++i)
            if (!(tiles[std::size_t(i)].slot == dragged)) ++k;
        return k;
    };
    Drop d;
    // Between the splitter and the mixer the row says the lane: the upper row is lane A, the lower row lane B. The lower row also counts under the splitter
    // (where the wire to lane B comes down) and under the mixer.
    const bool row1 = p.y >= kTileH + kGap / 2;
    bool inRegion = false;
    if (s >= 0 && m >= 0) {
        const auto sb = l.boxes[std::size_t(s)], mb = l.boxes[std::size_t(m)];
        inRegion = row1 ? (p.x >= sb.getX() && p.x < mb.getRight()) : (p.x > sb.getRight() && p.x < mb.getX());
    }
    if (inRegion) {
        d.lane = row1 ? 1 : 0;
        int before = m, last = -1;
        for (int i = s + 1; i < m; ++i) {
            if (tiles[std::size_t(i)].lane != d.lane || tiles[std::size_t(i)].slot == dragged) continue;
            last = i;
            if (before == m && l.boxes[std::size_t(i)].getCentreX() > p.x) before = i;
        }
        d.index = among(before);
        const int rowY = d.lane * (kTileH + kGap);
        int x;
        if (before != m) x = l.boxes[std::size_t(before)].getX() - kGap / 2;
        else if (last >= 0) x = l.boxes[std::size_t(last)].getRight() + kGap / 2;
        else x = l.boxes[std::size_t(s)].getRight() + kGap / 2;
        d.rect = {x - 1, rowY, 3, kTileH};
        return d;
    }
    // Elsewhere: in front of the nearest tile, or behind it when the point is past its middle.
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < n; ++i) {
        if (tiles[std::size_t(i)].slot == dragged) continue;
        const float dist = l.boxes[std::size_t(i)].getCentre().toFloat().getDistanceFrom(p.toFloat());
        if (dist < bestD) {
            bestD = dist;
            best = i;
        }
    }
    if (best < 0) return d;
    const bool after = p.x > l.boxes[std::size_t(best)].getCentreX();
    d.index = among(best) + (after ? 1 : 0);
    const auto box = l.boxes[std::size_t(best)];
    d.rect = {(after ? box.getRight() + kGap / 2 : box.getX() - kGap / 2) - 1, box.getY(), 3, kTileH};
    return d;
}

void RackPage::Map::paint(juce::Graphics& g) {
    const auto* look = lookOf(*this);
    const auto accent = look ? look->accent() : juce::Colour(0xffd9a441);
    const auto text = look ? look->text() : juce::Colours::white;
    const auto panel = look ? look->panel() : juce::Colour(0xff2a2723);
    const auto l = layout(getWidth());
    const int n = int(tiles.size());
    int s = -1, m = -1;
    for (int i = 0; i < n; ++i) {
        if (tiles[std::size_t(i)].slot.type == ModuleType::Splitter) s = i;
        if (tiles[std::size_t(i)].slot.type == ModuleType::Mixer) m = i;
    }
    const bool parallel = s >= 0 && m >= 0;
    g.setColour(text.withAlpha(0.35f));
    // The wires: along the upper row (what is shared, lane A and what comes after the mixer, in order), and along lane B.
    auto inLaneB = [&](int i) { return parallel && i > s && i < m && tiles[std::size_t(i)].lane == 1; };
    auto wire = [&](int from, int to) {
        const auto a = l.boxes[std::size_t(from)], b = l.boxes[std::size_t(to)];
        if (a.getY() == b.getY() && b.getX() > a.getRight()) g.fillRect(a.getRight(), a.getCentreY() - 1, b.getX() - a.getRight(), 2);
    };
    int previousUpper = -1, previousLower = -1;
    for (int i = 0; i < n; ++i) {
        int& previous = inLaneB(i) ? previousLower : previousUpper;
        if (previous >= 0) wire(previous, i);
        previous = i;
    }
    if (parallel) {
        int lastB = -1, firstB = -1;
        for (int i = s + 1; i < m; ++i)
            if (tiles[std::size_t(i)].lane == 1) {
                if (firstB < 0) firstB = i;
                lastB = i;
            }
        if (firstB >= 0) {
            const auto sp = l.boxes[std::size_t(s)], fb = l.boxes[std::size_t(firstB)], lb = l.boxes[std::size_t(lastB)], mx = l.boxes[std::size_t(m)];
            g.fillRect(sp.getCentreX() - 1, sp.getBottom(), 2, fb.getCentreY() - sp.getBottom());
            g.fillRect(sp.getCentreX(), fb.getCentreY() - 1, fb.getX() - sp.getCentreX(), 2);
            g.fillRect(mx.getCentreX() - 1, mx.getBottom(), 2, lb.getCentreY() - mx.getBottom());
            g.fillRect(lb.getRight(), lb.getCentreY() - 1, mx.getCentreX() - lb.getRight(), 2);
        }
    }
    for (int i = 0; i < n; ++i) {
        const auto& t = tiles[std::size_t(i)];
        const auto box = l.boxes[std::size_t(i)];
        const auto family = colourOf(t.slot.type);
        const bool lit = !t.hasSwitch || t.on;
        const bool sel = i == selected;
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
        if (parallel && i > s && i < m) {  // the lane
            g.setColour(text.withAlpha(0.55f));
            g.setFont(AmpLookAndFeel::font(10.0f, true));
            g.drawText(t.lane == 1 ? "B" : "A", box.withTrimmedTop(2).withTrimmedRight(8), juce::Justification::topRight);
        }
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
        g.setColour(text.withAlpha(0.35f));
        g.drawRoundedRectangle(l.plus.toFloat().reduced(0.5f), 9.0f, 1.0f);
        g.setColour(text.withAlpha(0.7f));
        g.setFont(AmpLookAndFeel::font(26.0f, true));
        g.drawText("+", l.plus, juce::Justification::centred);
    }
    if (!markerRect.isEmpty()) {  // where a dragged tile would land
        g.setColour(accent);
        g.fillRoundedRectangle(markerRect.toFloat().reduced(0.0f, 4.0f), 1.5f);
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
    const auto type = tiles[std::size_t(pressed_)].slot.type;
    if (type == ModuleType::Splitter || type == ModuleType::Mixer) return;  // the two ends of a parallel stay where they are
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        if (!container->isDragAndDropActive()) {
            // What follows the cursor is the tile alone, not the whole chain.
            const auto box = boxOf(pressed_);
            juce::Image image = createComponentSnapshot(box, true, 1.0f);
            image.multiplyAllAlphas(0.85f);
            const juce::Point<int> offset = box.getPosition() - e.getMouseDownPosition();
            container->startDragging(ModuleUnit::dragDescription(tiles[std::size_t(pressed_)].slot), this, juce::ScaledImage(image), false, &offset);
        }
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

void RackPage::removeParallelAsked() {
    // Lane B holds the second amp and cabinet; anything else in it goes too, so a rack with more in it asks first.
    int extra = 0;
    for (int i = 0; i < shown_.count; ++i)
        if (shown_.lane[std::size_t(i)] == 1 && shown_.slots[std::size_t(i)].type != ModuleType::Amp && shown_.slots[std::size_t(i)].type != ModuleType::Cabinet) ++extra;
    auto doIt = [this] {
        processor_.removeParallel();
        rebuild();
    };
    if (extra == 0) {
        doIt();
        return;
    }
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Remove the parallel",
                                       "Lane B has " + juce::String(extra) + " more module(s) besides the second amp and cabinet: they are removed with it. Lane A stays as it is.", "Remove", "Keep",
                                       this, juce::ModalCallbackFunction::create([this, doIt](int result) {
                                           if (result == 1) doIt();
                                       }));
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
            if (s.type == ModuleType::Splitter || s.type == ModuleType::Mixer) {
                removeParallelAsked();
                return;
            }
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
        tile.lane = shown_.lane[std::size_t(i)];
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
    if (before.count > 0 && shown_.count > before.count) {
        // After a parallel is added, the second amp opens; after any other add, the new module.
        for (int i = 0; i < shown_.count; ++i) {
            bool had = false;
            for (int k = 0; k < before.count; ++k) had = had || before.slots[std::size_t(k)] == shown_.slots[std::size_t(i)];
            if (!had && (shown_.slots[std::size_t(i)].type != ModuleType::Splitter && shown_.slots[std::size_t(i)].type != ModuleType::Mixer && shown_.slots[std::size_t(i)].type != ModuleType::Cabinet))
                pick = shown_.slots[std::size_t(i)];
        }
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

float RackPage::totalCpu() const {
    float total = 0.0f;
    for (int i = 0; i < shown_.count; ++i) total += processor_.rack().moduleCpu(shown_.slots[std::size_t(i)]);
    return total;
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
    menu.addItem(idBase - 1, processor.hasParallel() ? juce::String("Parallel (already there)") : juce::String("Parallel: two lanes, A and B"), processor.canAddParallel());
    // What a parallel can hold besides the pedals: a second amp and a second cabinet, or neither (a reverb or a dry path of its own, say).
    juce::PopupMenu lane;
    lane.addItem(idBase + int(ModuleType::Amp), "Second amp", processor.canAdd(ModuleType::Amp));
    lane.addItem(idBase + int(ModuleType::Cabinet), "Second cabinet", processor.canAdd(ModuleType::Cabinet));
    lane.addSeparator();
    for (const auto& cat : categories())
        for (auto t : cat.types)
            lane.addItem(idBase + kLaneBOffset + int(t), ModuleUnit::title({t, 0}), processor.hasParallel() && processor.canAdd(t));
    menu.addSubMenu("Lane B", lane);
    menu.addSeparator();
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
        if (choice == kBase - 1) {
            if (processor_.addParallel()) rebuild();
            return;
        }
        if (choice < kBase) return;
        if (choice >= kBase + kLaneBOffset) processor_.addModule(static_cast<ModuleType>(choice - kBase - kLaneBOffset), -1, 1);  // into lane B
        else processor_.addModule(static_cast<ModuleType>(choice - kBase), insertAt);
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
    const auto drop = map_.dropAt(map_.getLocalPoint(this, d.localPosition), slot);
    if (drop.rect != map_.markerRect) {
        map_.markerRect = drop.rect;
        map_.repaint();
    }
}

void RackPage::itemDragExit(const SourceDetails&) {
    map_.markerRect = {};
    map_.repaint();
}

void RackPage::itemDropped(const SourceDetails& d) {
    map_.markerRect = {};
    sas::SlotRef slot;
    if (ModuleUnit::parseDragDescription(d.description.toString(), slot)) {
        const auto drop = map_.dropAt(map_.getLocalPoint(this, d.localPosition), slot);
        const bool fixedLane = slot.type == ModuleType::Amp || slot.type == ModuleType::Cabinet;
        // Between the splitter and the mixer the row says the lane; the amps and cabinets keep their own.
        if (drop.lane >= 0 && !fixedLane) processor_.moveModuleToLane(slot, drop.lane, drop.index);
        else processor_.moveModule(slot, drop.index);
        rebuild();
        select(slot);
    }
    map_.repaint();
}
