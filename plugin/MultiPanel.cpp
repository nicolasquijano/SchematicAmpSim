#include "MultiPanel.h"

#include "I18n.h"

namespace {

juce::Colour slotColour(int k) { return juce::Colour::fromHSV(float(k) / float(SasProcessor::kMultiSlots), 0.55f, 0.95f, 1.0f); }

}  // namespace

// ------------------------------------------------------------------ the plane

MultiPad::MultiPad(SasProcessor& p) : processor_(p) {
    setTooltip(tr("Drag the ring to mix the impulses; click a point to choose its impulse; drag a point to move it; double click a point to put it back"));
    startTimerHz(30);
}

juce::Point<float> MultiPad::toPixel(float x, float y) const {
    const auto r = plane();
    return {r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight()};
}

void MultiPad::setParam(const juce::String& id, float value) {
    if (auto* q = processor_.apvts.getParameter(id)) q->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, value));
}

void MultiPad::gesture(const juce::String& id, bool begin) {
    if (auto* q = processor_.apvts.getParameter(id)) {
        if (begin) q->beginChangeGesture();
        else q->endChangeGesture();
    }
}

int MultiPad::nodeAt(juce::Point<float> p) const {
    int best = -1;
    float bestD = 14.0f;  // pixels
    for (int k = 0; k < SasProcessor::kMultiSlots; ++k) {
        const float x = processor_.apvts.getRawParameterValue(ids::multiId(ids::mcX, k))->load();
        const float y = processor_.apvts.getRawParameterValue(ids::multiId(ids::mcY, k))->load();
        const float d = toPixel(x, y).getDistanceFrom(p);
        if (d < bestD) {
            bestD = d;
            best = k;
        }
    }
    return best;
}

void MultiPad::mouseDown(const juce::MouseEvent& e) {
    moved_ = false;
    dragNode_ = nodeAt(e.position);
    dragCursor_ = dragNode_ < 0;
    if (dragCursor_) {
        gesture(ids::mcCursorX, true);
        gesture(ids::mcCursorY, true);
    } else {
        gesture(ids::multiId(ids::mcX, dragNode_), true);
        gesture(ids::multiId(ids::mcY, dragNode_), true);
    }
    mouseDrag(e);
}

void MultiPad::mouseDrag(const juce::MouseEvent& e) {
    if (dragNode_ >= 0 && !moved_) {
        if (e.getDistanceFromDragStart() < 4) return;  // still a click: the point stays where it is until the mouse really moves
        moved_ = true;
    }
    const auto r = plane();
    const float x = (e.position.x - r.getX()) / r.getWidth(), y = (e.position.y - r.getY()) / r.getHeight();
    if (dragCursor_) {
        setParam(ids::mcCursorX, x);
        setParam(ids::mcCursorY, y);
    } else if (dragNode_ >= 0) {
        setParam(ids::multiId(ids::mcX, dragNode_), x);
        setParam(ids::multiId(ids::mcY, dragNode_), y);
    }
}

void MultiPad::mouseUp(const juce::MouseEvent&) {
    if (dragCursor_) {
        gesture(ids::mcCursorX, false);
        gesture(ids::mcCursorY, false);
    } else if (dragNode_ >= 0) {
        gesture(ids::multiId(ids::mcX, dragNode_), false);
        gesture(ids::multiId(ids::mcY, dragNode_), false);
    }
    const int clicked = dragNode_ >= 0 && !moved_ ? dragNode_ : -1;
    dragNode_ = -1;
    dragCursor_ = false;
    if (clicked >= 0 && onNodeClicked) {
        const float x = processor_.apvts.getRawParameterValue(ids::multiId(ids::mcX, clicked))->load();
        const float y = processor_.apvts.getRawParameterValue(ids::multiId(ids::mcY, clicked))->load();
        const auto c = toPixel(x, y);
        onNodeClicked(clicked, localAreaToGlobal(juce::Rectangle<int>(int(c.x) - 8, int(c.y) - 8, 16, 16)));
    }
}

void MultiPad::mouseDoubleClick(const juce::MouseEvent& e) {
    const int k = nodeAt(e.position);
    if (k >= 0) {
        float x, y;
        sas::multiIrDefaultPosition(k, x, y);
        setParam(ids::multiId(ids::mcX, k), x);
        setParam(ids::multiId(ids::mcY, k), y);
    } else {
        setParam(ids::mcCursorX, 0.5f);
        setParam(ids::mcCursorY, 0.5f);
    }
}

void MultiPad::paint(juce::Graphics& g) {
    const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel());
    const auto accent = look ? look->accent() : juce::Colours::orange;
    const auto edge = look ? look->panelEdge() : juce::Colours::grey;
    const auto r = plane();
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
    g.setColour(edge.withAlpha(0.45f));
    for (int i = 1; i < 4; ++i) {
        g.drawVerticalLine(int(r.getX() + r.getWidth() * float(i) / 4.0f), r.getY() + 2.0f, r.getBottom() - 2.0f);
        g.drawHorizontalLine(int(r.getY() + r.getHeight() * float(i) / 4.0f), r.getX() + 2.0f, r.getRight() - 2.0f);
    }
    for (int k = 0; k < SasProcessor::kMultiSlots; ++k) {
        const float x = processor_.apvts.getRawParameterValue(ids::multiId(ids::mcX, k))->load();
        const float y = processor_.apvts.getRawParameterValue(ids::multiId(ids::mcY, k))->load();
        const auto c = toPixel(x, y);
        const bool loaded = processor_.multiImpulseLoaded(k);
        const float heard = processor_.multiWeightShown(k);
        const auto colour = slotColour(k);
        if (loaded && heard > 0.01f) {  // the glow: how much of it is heard
            g.setColour(colour.withAlpha(0.35f * heard));
            g.fillEllipse(c.x - 8.0f - 14.0f * heard, c.y - 8.0f - 14.0f * heard, 16.0f + 28.0f * heard, 16.0f + 28.0f * heard);
        }
        g.setColour(loaded ? colour : colour.withAlpha(0.35f));
        if (loaded) g.fillEllipse(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        else g.drawEllipse(c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, 1.2f);
        g.setColour(loaded ? juce::Colours::black.withAlpha(0.85f) : colour.withAlpha(0.6f));
        g.setFont(AmpLookAndFeel::font(10.0f, true));
        g.drawText(juce::String(k + 1), int(c.x) - 8, int(c.y) - 7, 16, 14, juce::Justification::centred);
    }
    // The cursor.
    const auto c = toPixel(processor_.apvts.getRawParameterValue(ids::mcCursorX)->load(), processor_.apvts.getRawParameterValue(ids::mcCursorY)->load());
    g.setColour(accent);
    g.drawEllipse(c.x - 11.0f, c.y - 11.0f, 22.0f, 22.0f, 2.0f);
    g.drawLine(c.x - 16.0f, c.y, c.x - 6.0f, c.y, 1.5f);
    g.drawLine(c.x + 6.0f, c.y, c.x + 16.0f, c.y, 1.5f);
    g.drawLine(c.x, c.y - 16.0f, c.x, c.y - 6.0f, 1.5f);
    g.drawLine(c.x, c.y + 6.0f, c.x, c.y + 16.0f, 1.5f);
}

// ------------------------------------------------------------------ the strips

MultiPanel::MultiPanel(SasProcessor& p) : processor_(p), pad_(p) {
    addAndMakeVisible(pad_);
    pad_.onNodeClicked = [this](int slot, juce::Rectangle<int> screenArea) {
        showLoadMenu(slot, juce::PopupMenu::Options().withTargetScreenArea(screenArea));
    };
    for (int k = 0; k < SasProcessor::kMultiSlots; ++k) {
        auto& s = strips_[k];
        s.name.setFont(AmpLookAndFeel::font(10.5f));
        s.name.setMinimumHorizontalScale(0.7f);
        s.note.setFont(AmpLookAndFeel::font(9.5f));
        s.note.setJustificationType(juce::Justification::centredLeft);
        for (juce::Button* b : std::initializer_list<juce::Button*>{&s.load, &s.clear, &s.solo}) b->setButtonText(tr(b->getButtonText()));
        s.load.onClick = [this, k] { showLoadMenu(k, juce::PopupMenu::Options().withTargetComponent(&strips_[k].load)); };
        s.clear.onClick = [this, k] { processor_.clearMultiImpulse(k); };
        s.solo.setClickingTogglesState(true);
        s.polarity.onClick = [this, k] { cyclePolarity(k); };
        s.polarity.setTooltip(tr("Polarity of this impulse: Auto (the biggest swing is made positive), + as it is, - upside down"));
        s.soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::multiId(ids::mcSolo, k), s.solo);
        s.levelAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids::multiId(ids::mcLevel, k), s.level);
        s.delayAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids::multiId(ids::mcDelay, k), s.delay);
        s.hpAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids::multiId(ids::mcHp, k), s.hp);
        s.lpAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids::multiId(ids::mcLp, k), s.lp);
        for (auto* sl : {&s.level, &s.delay, &s.hp, &s.lp}) {
            styleBar(*sl);
            addAndMakeVisible(*sl);
        }
        // Compact readings for the narrow bars.
        s.delay.textFromValueFunction = [](double v) { return juce::String(v, 2); };
        s.hp.textFromValueFunction = [](double v) { return v <= 20.5 ? juce::String("off") : juce::String(int(v + 0.5)); };
        s.lp.textFromValueFunction = [](double v) { return v >= 19500.0 ? juce::String("off") : v >= 1000.0 ? juce::String(v / 1000.0, 1) + "k" : juce::String(int(v + 0.5)); };
        for (auto* sl : {&s.delay, &s.hp, &s.lp}) sl->updateText();
        s.level.setDoubleClickReturnValue(true, 0.0);
        s.delay.setDoubleClickReturnValue(true, 0.0);
        s.hp.setDoubleClickReturnValue(true, 20.0);
        s.lp.setDoubleClickReturnValue(true, 20000.0);
        s.level.setTooltip(tr("The level of this impulse"));
        s.delay.setTooltip(tr("Delays this impulse (ms), to line it up with the others"));
        s.hp.setTooltip(tr("High-pass filter of this impulse (20 Hz = off)"));
        s.lp.setTooltip(tr("Low-pass filter of this impulse (20 kHz = off)"));
        s.solo.setTooltip(tr("Hear only the soloed impulses"));
        for (auto* c : std::initializer_list<juce::Component*>{&s.name, &s.note, &s.load, &s.clear, &s.polarity, &s.solo}) addAndMakeVisible(*c);
    }
    lookAndFeelChanged();
    startTimerHz(10);
    timerCallback();
}

MultiPanel::~MultiPanel() = default;

void MultiPanel::styleBar(juce::Slider& s) {
    s.setSliderStyle(juce::Slider::LinearBar);
    s.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 0, 0);
    s.setTextBoxIsEditable(false);
}

void MultiPanel::lookAndFeelChanged() {
    const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel());
    if (!look) return;
    for (auto& s : strips_) {
        for (auto* sl : {&s.level, &s.delay, &s.hp, &s.lp}) {
            sl->setColour(juce::Slider::trackColourId, look->accent().withAlpha(0.55f));
            sl->setColour(juce::Slider::backgroundColourId, juce::Colours::black.withAlpha(0.35f));
            sl->setColour(juce::Slider::textBoxTextColourId, look->text());
        }
        s.solo.setColour(juce::TextButton::buttonOnColourId, look->accent());
        s.solo.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        s.name.setColour(juce::Label::textColourId, look->text());
        s.note.setColour(juce::Label::textColourId, look->label());
    }
}

void MultiPanel::timerCallback() {
    for (int k = 0; k < SasProcessor::kMultiSlots; ++k) {
        auto& s = strips_[k];
        const auto name = processor_.multiImpulseName(k);
        s.name.setText(juce::String(k + 1) + "  " + (name.isEmpty() ? juce::String(tr("empty")) : name), juce::dontSendNotification);
        const auto info = processor_.multiAlignInfo(k);
        juce::String text;
        if (info.valid) {
            if (info.flipped) text << tr("flipped");
            if (info.delayMs >= 0.005) text << (text.isEmpty() ? "" : ", ") << "+" << juce::String(info.delayMs, 2) << " ms";
            if (text.isEmpty()) text = tr("in phase");
        }
        s.note.setText(text, juce::dontSendNotification);
        s.clear.setEnabled(processor_.multiImpulseLoaded(k));
        const int pol = juce::jlimit(0, 2, int(processor_.apvts.getRawParameterValue(ids::multiId(ids::mcPolarity, k))->load() + 0.5f));
        s.polarity.setButtonText(pol == 0 ? juce::String(tr("Auto")) : pol == 1 ? juce::String("+") : juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")));
        s.clear.setTooltip(tr("Remove this impulse"));
    }
}

void MultiPanel::resized() {
    constexpr int gap = 6;
    boxes_.clear();
    auto area = getLocalBounds();
    auto padArea = area.removeFromLeft(std::min(area.getWidth() / 4, kHeight - 2));
    area.removeFromLeft(gap);
    boxes_.push_back({padArea, tr("MULTI MIXER")});
    pad_.setBounds(padArea.reduced(4, 4).withTrimmedTop(16));
    const int n = SasProcessor::kMultiSlots;
    const int w = (area.getWidth() - gap * (n - 1)) / n;
    for (int k = 0; k < n; ++k) {
        auto r = area.removeFromLeft(w);
        area.removeFromLeft(gap);
        auto& s = strips_[k];
        s.bounds = r;
        boxes_.push_back({r, juce::String()});
        auto in = r.reduced(5, 4);
        in.removeFromTop(2);
        s.name.setBounds(in.removeFromTop(16));
        s.note.setBounds(in.removeFromTop(13));
        in.removeFromTop(2);
        auto row = in.removeFromTop(20);
        s.clear.setBounds(row.removeFromRight(22));
        row.removeFromRight(3);
        s.load.setBounds(row);
        in.removeFromTop(3);
        row = in.removeFromTop(20);
        s.solo.setBounds(row.removeFromRight(22));
        row.removeFromRight(3);
        s.polarity.setBounds(row);
        in.removeFromTop(3);
        for (auto* sl : {&s.level, &s.delay, &s.hp, &s.lp}) {
            in.removeFromTop(11);  // the caption, drawn in paint()
            sl->setBounds(in.removeFromTop(18));
            in.removeFromTop(3);
        }
    }
}

void MultiPanel::paint(juce::Graphics& g) {
    const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel());
    if (!look) return;
    paintPanelBoxes(g, *look, boxes_);
    g.setColour(look->label());
    g.setFont(AmpLookAndFeel::font(8.5f, true, 0.05f));
    for (int k = 0; k < SasProcessor::kMultiSlots; ++k) {
        const auto& s = strips_[k];
        g.setColour(slotColour(k));
        g.fillRoundedRectangle(float(s.bounds.getX() + 4), float(s.bounds.getY() + 4), 3.0f, 9.0f, 1.5f);
        g.setColour(look->label());
        int i = 0;
        for (const char* caption : {"LEVEL", "DELAY", "HI-PASS", "LO-PASS"}) {
            juce::Slider* sl = i == 0 ? &strips_[k].level : i == 1 ? &strips_[k].delay : i == 2 ? &strips_[k].hp : &strips_[k].lp;
            g.drawText(tr(caption), sl->getX(), sl->getY() - 11, sl->getWidth(), 11, juce::Justification::centredLeft);
            ++i;
        }
    }
}

void MultiPanel::cyclePolarity(int slot) {
    if (auto* q = processor_.apvts.getParameter(ids::multiId(ids::mcPolarity, slot))) {
        const int now = juce::jlimit(0, 2, int(processor_.apvts.getRawParameterValue(ids::multiId(ids::mcPolarity, slot))->load() + 0.5f));
        q->beginChangeGesture();
        q->setValueNotifyingHost(q->convertTo0to1(float((now + 1) % 3)));
        q->endChangeGesture();
    }
    timerCallback();
}

void MultiPanel::showLoadMenu(int slot, const juce::PopupMenu::Options& where) {
    juce::PopupMenu menu;
    for (int i = 0; i < SasProcessor::builtInImpulseResponseCount(); ++i) menu.addItem(100 + i, SasProcessor::builtInImpulseResponseName(i));
    menu.addSeparator();
    menu.addItem(1, tr("From file..."));
    menu.showMenuAsync(where, [this, slot](int choice) {
        if (choice == 1) chooseFile(slot);
        else if (choice >= 100) processor_.loadBuiltInMultiImpulse(choice - 100, slot);
    });
}

void MultiPanel::chooseFile(int slot) {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Choose a cabinet impulse response"), juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File{}) return;
        if (!processor_.loadMultiImpulse(file, slot))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim", tr("Could not read that file as an impulse response."));
    });
}
