#include "CircuitPage.h"

#include <algorithm>
#include <cmath>

#include "I18n.h"

#ifdef SAS_HAS_FACES
#include "BinaryData.h"
#endif

CircuitPage::CircuitPage(SasProcessor& processor, const AmpLookAndFeel& look) : processor_(processor), look_(look) {
    addAndMakeVisible(host_);  // before the buttons, so they stay above the drawings
    host_.setInterceptsMouseClicks(false, false);
}

CircuitPage::~CircuitPage() = default;

float CircuitPage::fractionOf(float ratio) {
    const float db = 20.0f * std::log10(std::max(ratio, 1.0e-5f));
    return std::clamp((db + kRangeDb) / kRangeDb, 0.0f, 1.0f);
}

// Quiet stages stay the colour of an unlit wire; from about -30 dB they go green, amber from -12 and red from -3.
juce::Colour CircuitPage::levelColour(float db) {
    const juce::Colour quiet(0xff6b5c44), green(0xff6aa84f), amber(0xfff0b53a), red(0xffe5533a);
    if (db <= -40.0f) return quiet;
    if (db < -30.0f) return quiet.interpolatedWith(green, (db + 40.0f) / 10.0f);
    if (db < -12.0f) return green;
    if (db < -3.0f) return green.interpolatedWith(amber, (db + 12.0f) / 9.0f);
    if (db < 0.0f) return amber.interpolatedWith(red, (db + 3.0f) / 3.0f);
    return red;
}

void CircuitPage::collect(Drawing& dr, juce::Drawable& d) {
    const auto id = d.getName();
    if (auto* shape = dynamic_cast<juce::DrawableShape*>(&d)) {
        if (id.startsWith("wire_")) {
            // "wire_V1", or "wire_MASTER-2" for a second piece of the same stage's wire
            const auto key = id.fromFirstOccurrenceOf("wire_", false, false).upToFirstOccurrenceOf("-", false, false);
            for (std::size_t i = 0; i < stages_.size(); ++i)
                if (stages_[i].key == key) dr.wires.push_back({i, shape});
        } else if (id.startsWith("ptr_")) {
            if (auto* path = dynamic_cast<juce::DrawablePath*>(shape)) dr.pointers[id.fromFirstOccurrenceOf("ptr_", false, false)] = path;
        } else if (id.startsWith("glow_")) {
            dr.glows[id.fromFirstOccurrenceOf("glow_", false, false).upToFirstOccurrenceOf("_", false, false)].push_back(shape);
        }
    }
    if (id.startsWith("pot_")) {
        const auto param = id.fromFirstOccurrenceOf("pot_", false, false).upToFirstOccurrenceOf("-", false, false);  // "treble-2": the same knob drawn again
        const auto it = uiLabels_.find(param);  // the name the amp gives its knob ("VOLUME", "MASTER"...)
        if (!id.contains("-")) dr.tags.push_back({&d, it != uiLabels_.end() ? it->second : param.toUpperCase(), 0});  // the copies carry no name
    } else if (id.startsWith("tube_")) {
        const auto rest = id.fromFirstOccurrenceOf("tube_", false, false);  // "V1", or "PA-EL34": what follows the dash is the name shown
        dr.tags.push_back({&d, rest.contains("-") ? rest.fromFirstOccurrenceOf("-", false, false) : rest, 1});
    } else if (id == "jack_in") {
        dr.tags.push_back({&d, "INPUT", 0});
    } else if (id.startsWith("jack_") && !id.endsWith("_hole")) {  // an amp with several inputs: "jack_BRIGHT" is named BRIGHT
        dr.tags.push_back({&d, id.fromFirstOccurrenceOf("jack_", false, false), 0});
    } else if (id == "spk_cone" || id == "spk_cone_p") {
        dr.tags.push_back({&d, "SPEAKER", 0});
    } else if (id == "next_power") {
        dr.next = &d;
    } else if (id.length() > 6 && id.substring(0, 3) == "val" && id.substring(4, 5) == "_") {
        juce::String text = id.substring(5).fromFirstOccurrenceOf("_", false, false);
        if (text.endsWithChar('u')) text = text.dropLastCharacters(1) + juce::String(juce::CharPointer_UTF8("\xc2\xb5"));
        else if (text.endsWithChar('R')) text = text.dropLastCharacters(1) + juce::String(juce::CharPointer_UTF8("\xce\xa9"));
        dr.values.push_back({&d, text, char(id[3])});
    } else if (id.startsWith("lbl_")) {
        dr.labels[id.fromFirstOccurrenceOf("lbl_", false, false)] = &d;
    }
    for (auto* child : d.getChildren())
        if (auto* sub = dynamic_cast<juce::Drawable*>(child)) collect(dr, *sub);
}

void CircuitPage::load(int which, const sas::AmpDef& def) {
    auto& dr = drawings_[std::size_t(which)];
    if (dr.art) host_.removeChildComponent(dr.art.get());
    dr = Drawing{};
#ifdef SAS_HAS_FACES
    int size = 0;
    const std::string resource = def.id + (which == powerIndex() ? "_circuit_power_svg" : which == 0 ? "_circuit_svg" : "_circuit_" + std::to_string(which) + "_svg");
    if (const char* data = BinaryData::getNamedResource(resource.c_str(), size))
        if (auto xml = juce::XmlDocument::parse(juce::String::fromUTF8(data, size)))
            dr.art = juce::Drawable::createFromSVG(*xml);
#else
    juce::ignoreUnused(def);
#endif
    if (!dr.art) return;
    host_.addChildComponent(*dr.art);
    dr.art->setInterceptsMouseClicks(false, false);
    collect(dr, *dr.art);
    for (const auto& t : dr.tags) {  // a pot whose name is a knob of the amp is a live control
        if (!t.item->getName().startsWith("pot_")) continue;
        const auto name = t.item->getName().fromFirstOccurrenceOf("pot_", false, false);
        const auto base = name.upToFirstOccurrenceOf("-", false, false);
        if (std::find(uiParams_.begin(), uiParams_.end(), base) == uiParams_.end()) continue;
        Pot p;
        p.body = t.item;
        p.name = name;
        p.param = base;
        p.centre = t.item->getDrawableBounds().getCentre();
        p.length = t.item->getDrawableBounds().getWidth() * 0.31f;
        if (const auto it = dr.pointers.find(name); it != dr.pointers.end()) p.pointer = it->second;
        dr.pots.push_back(p);
    }
}

void CircuitPage::setAmp(const sas::AmpDef& def) {
    stages_.clear();
    auto add = [&](const juce::String& key) {
        Stage s;
        s.key = key;
        s.name = tr(key);
        stages_.push_back(s);
    };
    add("INPUT");
    if (!def.nam) {
        if (def.probes.empty()) add("PREAMP");
        for (const auto& p : def.probes) add(juce::String(p.label));
        for (const auto& p : def.powerProbes) add(juce::String(p.label));
    }
    add("SPEAKER");

    uiParams_.clear();
    uiLabels_.clear();
    for (const auto& u : def.ui) {
        uiParams_.push_back(juce::String(u.param));
        uiLabels_[juce::String(u.param)] = juce::String(u.label);
    }
    hover_ = drag_ = {};
    for (auto& dr : drawings_)
        if (dr.art) host_.removeChildComponent(dr.art.get());
    channels_ = def.circuitChannels;
    channelSel_.clear();
    for (const auto& ch : channels_) {
        int index = -1;
        for (std::size_t i = 0; i < def.preamp.selectors.size(); ++i)
            if (!ch.selector.empty() && def.preamp.selectors[i].name == ch.selector) index = int(i);
        channelSel_.push_back(index);
    }
    drawings_.clear();
    drawings_.resize(std::max<std::size_t>(1, channels_.size()) + 1);
    for (int i = 0; i < int(drawings_.size()); ++i) load(i, def);
    // The buttons: one per channel drawing (named as the amp names the channel), then the power amp.
    viewButtons_.clear();
    const bool several = channelCount() > 1 || hasPowerDrawing();
    for (int i = 0; i < int(drawings_.size()); ++i) {
        if (!drawings_[std::size_t(i)].art) continue;
        auto b = std::make_unique<juce::TextButton>(i == powerIndex() ? tr("POWER") : channels_.empty() ? tr("PREAMP") : tr(juce::String(channels_[std::size_t(i)].label)));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(31);
        b->getProperties().set("view", i);
        b->onClick = [this, i] { setView(i); };
        addChildComponent(*b);
        b->setVisible(several);
        viewButtons_.push_back(std::move(b));
    }
    if (view_ >= int(drawings_.size()) || !drawings_[std::size_t(view_)].art) view_ = 0;
    lastChannel_ = -1;
    setView(view_);
    updatePots();
}

// The channel the amp is set to: the input jack, or a selector, as the amp's own channels are chosen.
int CircuitPage::currentChannel() const {
    const int jack = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    for (std::size_t i = 0; i < channels_.size(); ++i) {
        const auto& ch = channels_[i];
        if (!ch.jacks.empty() && std::find(ch.jacks.begin(), ch.jacks.end(), jack) != ch.jacks.end()) return int(i);
        if (channelSel_[i] >= 0 && channelSel_[i] < 6 &&
            std::find(ch.positions.begin(), ch.positions.end(), int(processor_.apvts.getRawParameterValue(ids::sel[channelSel_[i]])->load() + 0.5f)) != ch.positions.end())
            return int(i);
    }
    return -1;
}

void CircuitPage::setView(int v) {
    view_ = (v >= 0 && v < int(drawings_.size()) && drawings_[std::size_t(v)].art) ? v : 0;
    for (std::size_t i = 0; i < drawings_.size(); ++i)
        if (drawings_[i].art) drawings_[i].art->setVisible(int(i) == view_);
    for (auto& b : viewButtons_) b->setToggleState(int(b->getProperties()["view"]) == view_, juce::dontSendNotification);
    hover_ = {};
    zoom_ = 1.0f;
    pan_ = {};
    overview_ = false;
    resized();
    repaint();
}

void CircuitPage::resized() {
    auto area = getLocalBounds().toFloat().reduced(10.0f, 6.0f);
    area.removeFromTop(22.0f);
    strip_ = area.removeFromBottom(64.0f);
    area.removeFromBottom(26.0f);  // room for the names and values under the pots
    {
        auto bar = getLocalBounds().reduced(10, 4).removeFromTop(22);
        for (int i = int(viewButtons_.size()) - 1; i >= 0; --i) {
            viewButtons_[std::size_t(i)]->setBounds(bar.removeFromRight(std::max(72, viewButtons_[std::size_t(i)]->getBestWidthForHeight(22) + 10)));
            bar.removeFromRight(4);
        }
    }
    // The drawings live in a host that clips them to the schematic's area; a wide one starts at a readable size (not squeezed to fit) and the player
    // drags it along, wheels to zoom and double-clicks to see all of it.
    host_.setBounds(area.toNearestInt());
    const auto local = juce::Rectangle<float>(0.0f, 0.0f, float(host_.getWidth()), float(host_.getHeight()));
    for (auto& dr : drawings_) {
        if (!dr.art) continue;
        const auto src = dr.art->getDrawableBounds();
        const juce::RectanglePlacement placement(juce::RectanglePlacement::centred);
        auto base = local;
        const float fitScale = std::min(local.getWidth() / src.getWidth(), local.getHeight() / src.getHeight());
        if (!overview_ && fitScale < kReadableScale)
            base = juce::Rectangle<float>(0.0f, std::max(0.0f, (local.getHeight() - src.getHeight() * kReadableScale) * 0.5f), src.getWidth() * kReadableScale, src.getHeight() * kReadableScale);
        const auto c = local.getCentre();
        const auto shown = juce::Rectangle<float>(base.getWidth() * zoom_, base.getHeight() * zoom_).withCentre(c + (base.getCentre() - c) * zoom_ + pan_);
        dr.fit = placement.getTransformToFit(src, shown).translated(float(host_.getX()), float(host_.getY()));
        dr.art->setTransformToFit(shown, placement);
    }
}

void CircuitPage::tick() {
    const int n = processor_.meterCount();
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < int(stages_.size()); ++i) {
        auto& s = stages_[std::size_t(i)];
        const float ratio = i < n ? processor_.takeMeter(i) : 0.0f;
        const float f = fractionOf(ratio);
        s.levelDb = 20.0f * std::log10(std::max(ratio, 1.0e-5f));
        s.shown = f > s.shown ? f : s.shown * 0.82f + f * 0.18f;  // up at once, down smoothly
        if (f >= s.peak || now - s.peakAtMs > 1200) {
            s.peak = f;
            s.peakAtMs = now;
        }
        if (ratio >= 1.0f) s.clipAtMs = now;
    }
    // The drawings: each wire takes the colour of its stage's level, each tube glows with the signal that goes through it.
    for (auto& dr : drawings_) {
        for (auto& w : dr.wires) {
            const auto& s = stages_[w.stage];
            w.shape->setStrokeFill(juce::FillType(levelColour(-kRangeDb + s.shown * kRangeDb)));
        }
        for (auto& [name, shapes] : dr.glows)
            for (std::size_t i = 0; i < stages_.size(); ++i)
                if (stages_[i].key == name)
                    for (auto* g : shapes) g->setAlpha(0.35f + 0.65f * std::clamp(stages_[i].shown * 1.2f, 0.0f, 1.0f));
    }
    // The tubes of the power amp take the level of the stage that drives them.
    for (auto& [name, key] : {std::pair<const char*, const char*>{"EL34A", "POWER"}, {"EL34B", "POWER"}, {"V3", "PHASE"}})
        for (auto& dr : drawings_)
            if (const auto it = dr.glows.find(name); it != dr.glows.end())
                for (const auto& s : stages_)
                    if (s.key == key)
                        for (auto* g : it->second) g->setAlpha(0.35f + 0.65f * std::clamp(s.shown * 1.2f, 0.0f, 1.0f));
    // The drawing follows the amp's channel when the player changes it (the buttons choose freely until then).
    if (const int ch = currentChannel(); ch != lastChannel_) {
        lastChannel_ = ch;
        if (ch >= 0 && view_ != powerIndex() && view_ != ch) setView(ch);
    }
    updatePots();
    repaint();
}

// The pointer of each pot follows its parameter: the knobs turn through 270 degrees, from lower left to lower right.
void CircuitPage::updatePots() {
    for (auto& dr : drawings_)
        for (auto& p : dr.pots) {
            const auto* param = processor_.apvts.getParameter(p.param);
            if (!param || !p.pointer) continue;
            const float v = param->getValue();
            if (std::abs(v - p.shown) < 1.0e-4f) continue;
            p.shown = v;
            const float a = juce::degreesToRadians(-135.0f + 270.0f * v);
            const auto tip = p.centre + juce::Point<float>(std::sin(a), -std::cos(a)) * p.length;
            juce::Path line;
            line.startNewSubPath(p.centre);
            line.lineTo(tip);
            p.pointer->setPath(line);
        }
}

CircuitPage::PotRef CircuitPage::potAt(juce::Point<float> pos) const {
    const auto& dr = drawings_[std::size_t(view_)];
    for (std::size_t i = 0; i < dr.pots.size(); ++i) {
        const auto b = dr.pots[i].body->getDrawableBounds().transformedBy(dr.fit);
        if (b.expanded(4.0f).contains(pos)) return {view_, int(i)};
    }
    return {};
}

juce::String CircuitPage::potParam(int i) const {
    for (const auto& dr : drawings_) {
        if (i < int(dr.pots.size())) return dr.pots[std::size_t(i)].param;
        i -= int(dr.pots.size());
    }
    return {};
}

void CircuitPage::nudgePot(int i, float delta) {
    Pot* pot = nullptr;
    for (auto& dr : drawings_) {
        if (i < int(dr.pots.size())) {
            pot = &dr.pots[std::size_t(i)];
            break;
        }
        i -= int(dr.pots.size());
    }
    if (!pot) return;
    if (auto* param = processor_.apvts.getParameter(pot->param)) {
        param->setValueNotifyingHost(std::clamp(param->getValue() + delta, 0.0f, 1.0f));
        updatePots();
        repaint();
    }
}

void CircuitPage::mouseMove(const juce::MouseEvent& e) {
    const auto h = potAt(e.position);
    const auto& dr = drawings_[std::size_t(view_)];
    const bool onNext = dr.next && dr.next->getDrawableBounds().transformedBy(dr.fit).expanded(6.0f).contains(e.position);
    if (h.drawing != hover_.drawing || h.index != hover_.index) {
        hover_ = h;
        repaint();
    }
    setMouseCursor(h.drawing >= 0 ? juce::MouseCursor::UpDownResizeCursor : onNext ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void CircuitPage::mouseExit(const juce::MouseEvent&) {
    if (hover_.drawing >= 0 && drag_.drawing < 0) {
        hover_ = {};
        repaint();
    }
}

void CircuitPage::mouseDown(const juce::MouseEvent& e) {
    drag_ = potAt(e.position);
    if (drag_.drawing < 0) {  // the arrow at the end of the preamp leads on to the power amp
        const auto& dr = drawings_[std::size_t(view_)];
        if (view_ != powerIndex() && hasPowerDrawing() && dr.next && dr.next->getDrawableBounds().transformedBy(dr.fit).expanded(6.0f).contains(e.position)) {
            setView(powerIndex());
            return;
        }
        if (host_.getBounds().toFloat().contains(e.position)) {  // anywhere else on the drawing: drag it along
            panning_ = true;
            panAnchor_ = e.position - pan_;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        return;
    }
    hover_ = drag_;
    dragY_ = e.position.y;
    if (const auto* pot = potOf(drag_))
        if (auto* param = processor_.apvts.getParameter(pot->param)) param->beginChangeGesture();
}

void CircuitPage::mouseDrag(const juce::MouseEvent& e) {
    if (panning_) {
        pan_ = e.position - panAnchor_;
        resized();
        repaint();
        return;
    }
    if (drag_.drawing < 0) return;
    const float dy = dragY_ - e.position.y;
    dragY_ = e.position.y;
    int flat = drag_.index;
    for (int d = 0; d < drag_.drawing; ++d) flat += int(drawings_[std::size_t(d)].pots.size());
    nudgePot(flat, dy / (e.mods.isShiftDown() ? 800.0f : 200.0f));
}

void CircuitPage::mouseUp(const juce::MouseEvent& e) {
    if (panning_) {
        panning_ = false;
        mouseMove(e);
        return;
    }
    if (drag_.drawing < 0) return;
    if (const auto* pot = potOf(drag_))
        if (auto* param = processor_.apvts.getParameter(pot->param)) param->endChangeGesture();
    drag_ = {};
    mouseMove(e);
}

void CircuitPage::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto r = potAt(e.position);
    const auto* pot = potOf(r);
    if (!pot) {  // on the drawing: all of it, or back to the readable size
        if (host_.getBounds().toFloat().contains(e.position)) {
            overview_ = !overview_;
            zoom_ = 1.0f;
            pan_ = {};
            resized();
            repaint();
        }
        return;
    }
    if (auto* param = processor_.apvts.getParameter(pot->param)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->getDefaultValue());
        param->endChangeGesture();
        updatePots();
        repaint();
    }
}

void CircuitPage::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const auto r = potAt(e.position);
    const auto* pot = potOf(r);
    if (!pot) {  // the wheel over the drawing zooms it about the pointer
        if (!host_.getBounds().toFloat().contains(e.position) || w.deltaY == 0.0f) return;
        const float newZoom = std::clamp(zoom_ * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f), 0.5f, 8.0f);
        const float f = newZoom / zoom_;
        const auto p = e.position - host_.getBounds().toFloat().getCentre();
        pan_ = p - (p - pan_) * f;
        zoom_ = newZoom;
        resized();
        repaint();
        return;
    }
    auto* param = processor_.apvts.getParameter(pot->param);
    if (!param) return;
    int flat = r.index;
    for (int d = 0; d < r.drawing; ++d) flat += int(drawings_[std::size_t(d)].pots.size());
    param->beginChangeGesture();
    nudgePot(flat, (w.deltaY > 0 ? 1.0f : -1.0f) * 0.02f);
    param->endChangeGesture();
}

void CircuitPage::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat().reduced(10.0f, 6.0f);
    g.setColour(look_.label());
    g.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
    g.drawText(tr("SIGNAL CHAIN"), area.withHeight(16.0f).toNearestInt(), juce::Justification::centredLeft);
    if (!hasDrawing()) paintBlocks(g, area);
}

void CircuitPage::paintOverChildren(juce::Graphics& g) {
    if (!hasDrawing()) return;
    const auto& dr = drawings_[std::size_t(view_)];
    g.saveState();
    g.reduceClipRegion(host_.getBounds());  // the names and values stay inside the schematic's area when it is zoomed or dragged
    // The names of the tubes and pots, under or beside them.
    g.setFont(AmpLookAndFeel::font(11.5f, true, 0.06f));
    for (const auto& t : dr.tags) {
        const auto b = t.item->getDrawableBounds().transformedBy(dr.fit);
        g.setColour(look_.label());
        const juce::String text = tr(t.text);
        if (t.kind == 1) g.drawText(text, juce::Rectangle<float>(b.getRight() + 6.0f, b.getCentreY() - 8.0f, 70.0f, 16.0f).toNearestInt(), juce::Justification::centredLeft);
        else if (const auto name = t.item->getName(); name.startsWith("pot_") && dr.labels.count(name.fromFirstOccurrenceOf("pot_", false, false)) != 0) {
            const auto c = dr.labels.at(name.fromFirstOccurrenceOf("pot_", false, false))->getDrawableBounds().transformedBy(dr.fit).getCentre();
            g.drawText(text, juce::Rectangle<float>(c.x - 60.0f, c.y - 16.0f, 120.0f, 16.0f).toNearestInt(), juce::Justification::centred);
        } else {
            // A name is as wide as the room between the pots (a panel with a dozen of them): longer ones shrink to fit.
            float room = 100.0f;
            for (const auto& other : dr.tags)
                if (other.item != t.item && other.kind == 0 && std::abs(other.item->getDrawableBounds().transformedBy(dr.fit).getCentreY() - b.getCentreY()) < 4.0f)
                    room = std::min(room, std::abs(other.item->getDrawableBounds().transformedBy(dr.fit).getCentreX() - b.getCentreX()) - 4.0f);
            auto box = juce::Rectangle<float>(b.getCentreX() - room * 0.5f, b.getBottom() + (t.item->getName().startsWith("pot_") ? 28.0f : 4.0f), room, 16.0f);
            box.setX(std::max(box.getX(), 4.0f));  // a jack at the edge of the drawing keeps its name inside the page
            g.drawFittedText(text, box.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
    }
    g.setFont(AmpLookAndFeel::font(9.5f));  // the values of the components
    g.setColour(look_.label().withAlpha(0.9f));
    for (const auto& v : dr.values) {
        const auto c = v.item->getDrawableBounds().transformedBy(dr.fit).getCentre();
        const float w = 48.0f;
        const auto box = v.align == 'R' ? juce::Rectangle<float>(c.x - w, c.y - 7.0f, w, 14.0f)
                         : v.align == 'C' ? juce::Rectangle<float>(c.x - w * 0.5f, c.y - 7.0f, w, 14.0f)
                                           : juce::Rectangle<float>(c.x, c.y - 7.0f, w, 14.0f);
        g.drawText(v.text, box.toNearestInt(), v.align == 'R' ? juce::Justification::centredRight : v.align == 'C' ? juce::Justification::centred : juce::Justification::centredLeft);
    }
    if (view_ != powerIndex() && hasPowerDrawing() && dr.next) {  // the way on to the power amp
        const auto b = dr.next->getDrawableBounds().transformedBy(dr.fit);
        g.setColour(look_.label());
        g.drawText(tr("POWER"), juce::Rectangle<float>(b.getCentreX() - 50.0f, b.getBottom() + 4.0f, 100.0f, 16.0f).toNearestInt(), juce::Justification::centred);
    }
    for (std::size_t i = 0; i < dr.pots.size(); ++i) {  // the value of each pot under its name; a ring on the one under the mouse
        const auto b = dr.pots[i].body->getDrawableBounds().transformedBy(dr.fit);
        const bool hovered = hover_.drawing == view_ && hover_.index == int(i);
        if (dr.pots[i].name != dr.pots[i].param && !hovered) continue;  // the copies of a knob show no value
        if (const auto* param = processor_.apvts.getParameter(dr.pots[i].param)) {
            g.setColour(hovered ? look_.accent() : look_.text().withAlpha(0.8f));
            g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
            auto at = juce::Rectangle<float>(b.getCentreX() - 30.0f, b.getBottom() + 42.0f, 60.0f, 14.0f);
            if (const auto it = dr.labels.find(dr.pots[i].name); it != dr.labels.end()) {
                const auto c = it->second->getDrawableBounds().transformedBy(dr.fit).getCentre();
                at = juce::Rectangle<float>(c.x - 30.0f, c.y + 1.0f, 60.0f, 14.0f);
            }
            g.drawText(param->getCurrentValueAsText(), at.toNearestInt(), juce::Justification::centred);
        }
        if (hovered) {
            g.setColour(look_.accent());
            g.drawEllipse(b.expanded(4.0f), 2.0f);
        }
    }
    g.restoreState();
    paintStrip(g, strip_);
}

// The stages as a row of horizontal bars (the schematic's meters): the name, the bar with its recent peak, the level in dB.
void CircuitPage::paintStrip(juce::Graphics& g, juce::Rectangle<float> area) {
    if (stages_.empty()) return;
    const auto now = juce::Time::getMillisecondCounter();
    g.setColour(look_.panelEdge().withAlpha(0.6f));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1.0f);
    const int n = int(stages_.size());
    const float colW = area.getWidth() / float(n);
    for (int i = 0; i < n; ++i) {
        const auto& s = stages_[std::size_t(i)];
        const auto col = area.withX(area.getX() + colW * float(i)).withWidth(colW).reduced(6.0f, 8.0f);
        const bool clipping = now - s.clipAtMs < 1500;
        g.setColour(clipping ? juce::Colour(0xfff08a70) : look_.text());
        g.setFont(AmpLookAndFeel::font(11.0f, true, 0.04f));
        g.drawText(s.name, col.withHeight(16.0f).toNearestInt(), juce::Justification::centredLeft);
        const auto well = juce::Rectangle<float>(col.getX(), col.getY() + 20.0f, col.getWidth(), 10.0f);
        g.setColour(look_.background().brighter(0.05f));
        g.fillRoundedRectangle(well, 3.0f);
        const float db = -kRangeDb + s.shown * kRangeDb;
        g.setColour(levelColour(std::max(db, -29.0f)));
        g.fillRoundedRectangle(well.withWidth(well.getWidth() * s.shown), 3.0f);
        g.setColour(look_.text().withAlpha(0.85f));
        g.fillRect(well.getX() + well.getWidth() * s.peak - 1.0f, well.getY() - 2.0f, 2.0f, well.getHeight() + 4.0f);
        g.setColour(clipping ? juce::Colour(0xffff5a3c) : look_.label());
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        const juce::String dbText = s.levelDb <= -99.0f ? juce::String("--") : juce::String(juce::roundToInt(s.levelDb)) + " dB";
        g.drawText(dbText, juce::Rectangle<float>(col.getX(), col.getY() + 33.0f, col.getWidth(), 14.0f).toNearestInt(), juce::Justification::centredLeft);
    }
}

// An amp without a drawing: the stages as blocks in a row, each with a vertical bar.
void CircuitPage::paintBlocks(juce::Graphics& g, juce::Rectangle<float> area) {
    if (stages_.empty()) return;
    const auto now = juce::Time::getMillisecondCounter();
    const int n = int(stages_.size());
    const float colW = area.getWidth() / float(n);
    const float boxW = std::min(colW - 16.0f, 120.0f);
    const float boxH = 44.0f;
    const float top = area.getY() + 26.0f;
    const float barTop = top + boxH + 14.0f;
    const float barBottom = area.getBottom() - 64.0f;
    const float barW = 22.0f;

    for (int i = 0; i < n; ++i) {
        const auto& s = stages_[std::size_t(i)];
        const float cx = area.getX() + colW * (float(i) + 0.5f);
        const bool clipping = now - s.clipAtMs < 1500;
        const auto edge = clipping ? juce::Colour(0xffd9573c) : look_.panelEdge();
        const juce::Rectangle<float> box(cx - boxW * 0.5f, top, boxW, boxH);
        g.setColour(clipping ? juce::Colour(0xff3a1f1a) : look_.panel());
        g.fillRoundedRectangle(box, 5.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(box, 5.0f, 1.2f);
        g.setColour(clipping ? juce::Colour(0xfff08a70) : look_.text());
        g.setFont(AmpLookAndFeel::font(13.0f, true, 0.04f));
        g.drawText(s.name, box.toNearestInt(), juce::Justification::centred);
        if (i + 1 < n) {  // the wire to the next stage
            g.setColour(look_.panelEdge());
            const float y = top + boxH * 0.5f;
            g.drawLine(cx + boxW * 0.5f, y, cx + colW - boxW * 0.5f, y, 1.5f);
        }

        // The meter: a bar from green to amber to red over kRangeDb, the recent peak as a line.
        const juce::Rectangle<float> well(cx - barW * 0.5f, barTop, barW, barBottom - barTop);
        g.setColour(look_.background().brighter(0.05f));
        g.fillRoundedRectangle(well, 3.0f);
        const float h = well.getHeight() * s.shown;
        const auto fillRect = juce::Rectangle<float>(well.getX(), well.getBottom() - h, well.getWidth(), h);
        const float db = -kRangeDb + s.shown * kRangeDb;
        g.setColour(db > -3.0f ? juce::Colour(0xffd9573c) : db > -12.0f ? look_.accent() : juce::Colour(0xff6aa84f));
        g.fillRoundedRectangle(fillRect, 3.0f);
        const float py = well.getBottom() - well.getHeight() * s.peak;
        g.setColour(look_.text().withAlpha(0.85f));
        g.fillRect(well.getX() - 3.0f, py - 1.0f, well.getWidth() + 6.0f, 2.0f);

        g.setColour(clipping ? juce::Colour(0xffff5a3c) : look_.panelEdge());
        g.fillEllipse(cx - 4.0f, barBottom + 10.0f, 8.0f, 8.0f);
        g.setColour(look_.label());
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        const juce::String dbText = s.levelDb <= -99.0f ? juce::String("--") : juce::String(juce::roundToInt(s.levelDb)) + " dB";
        g.drawText(dbText, juce::Rectangle<float>(cx - colW * 0.5f, barBottom + 22.0f, colW, 16.0f).toNearestInt(), juce::Justification::centred);
    }

    g.setColour(look_.label().withAlpha(0.8f));
    g.setFont(AmpLookAndFeel::font(10.5f));
    g.drawText(tr("Bar: level now. Line: recent peak. Red: at the limit."), area.withTrimmedTop(area.getHeight() - 16.0f).toNearestInt(),
               juce::Justification::centredLeft);
}
