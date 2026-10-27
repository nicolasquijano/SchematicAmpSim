#include "PluginEditor.h"

#include <cmath>

// ------------------------------------------------------------------ look

juce::FontOptions AmpLookAndFeel::font(float size, bool bold, float tracking) {
    auto f = juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain);
    return tracking != 0.0f ? f.withKerningFactor(tracking) : f;
}

AmpLookAndFeel::AmpLookAndFeel() : theme_(EditorTheme::from(sas::ThemeDef{})) {
#if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName("Segoe UI");
#elif JUCE_MAC
    setDefaultSansSerifTypefaceName("Helvetica Neue");
#endif
    setTheme(theme_);
}

void AmpLookAndFeel::setTheme(const EditorTheme& t) {
    theme_ = t;
    const auto body = t.panel.brighter(0.12f);
    setColour(juce::ResizableWindow::backgroundColourId, t.background);
    setColour(juce::Label::textColourId, t.text);
    setColour(juce::Slider::textBoxTextColourId, t.text.withAlpha(0.85f));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, t.accent.withAlpha(0.4f));
    setColour(juce::ComboBox::backgroundColourId, body);
    setColour(juce::ComboBox::outlineColourId, t.edge);
    setColour(juce::ComboBox::textColourId, t.text);
    setColour(juce::ComboBox::arrowColourId, t.accent);
    setColour(juce::PopupMenu::backgroundColourId, body);
    setColour(juce::PopupMenu::textColourId, t.text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha(0.35f));
    setColour(juce::PopupMenu::highlightedTextColourId, t.text);
    setColour(juce::TextButton::buttonColourId, body);
    setColour(juce::TextButton::buttonOnColourId, t.accent.withAlpha(0.25f));
    setColour(juce::TextButton::textColourOffId, t.text.withAlpha(0.75f));
    setColour(juce::TextButton::textColourOnId, t.accent);
    setColour(juce::ToggleButton::textColourId, t.text.withAlpha(0.85f));
    setColour(juce::ToggleButton::tickColourId, t.accent);
    setColour(juce::ListBox::backgroundColourId, t.panel);
    setColour(juce::ListBox::outlineColourId, t.edge);
}

void AmpLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                      float endAngle, juce::Slider&) {
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(3.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float a = startAngle + (endAngle - startAngle) * pos;
    const auto& t = theme_;

    // Tick marks 0..10 around the knob (the ends and the middle longer).
    constexpr int ticks = 11;
    for (int i = 0; i < ticks; ++i) {
        const float ta = startAngle + (endAngle - startAngle) * float(i) / float(ticks - 1);
        const bool major = i == 0 || i == ticks - 1 || i == ticks / 2;
        const juce::Point<float> d(std::sin(ta), -std::cos(ta));
        const float r0 = radius - (major ? 5.0f : 3.0f);
        g.setColour(t.label.withAlpha(major ? 0.9f : 0.55f));
        g.drawLine({c + d * r0, c + d * radius}, major ? 1.6f : 1.0f);
    }

    // Value arc just inside the ticks, with a soft glow.
    const float arcR = radius - 8.5f;
    juce::Path track, value;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    value.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, a, true);
    g.setColour(t.edge.withAlpha(0.8f));
    g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.accent.withAlpha(0.22f));
    g.strokePath(value, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(t.accent);
    g.strokePath(value, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body: a shadow, a dark skirt and a domed cap.
    const float skirtR = (arcR - 3.5f);
    const float capR = skirtR * 0.80f;
    juce::Path skirt;
    skirt.addEllipse(c.x - skirtR, c.y - skirtR, skirtR * 2, skirtR * 2);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 7, {0, 2}).drawForPath(g, skirt);
    g.setGradientFill(juce::ColourGradient(t.knobBottom.brighter(0.15f), c.x, c.y - skirtR, t.knobBottom.darker(0.6f), c.x, c.y + skirtR, false));
    g.fillPath(skirt);
    g.setColour(t.edge.withAlpha(0.9f));
    g.drawEllipse(c.x - skirtR, c.y - skirtR, skirtR * 2, skirtR * 2, 1.0f);

    g.setGradientFill(juce::ColourGradient(t.knobTop, c.x - capR * 0.5f, c.y - capR * 0.9f, t.knobBottom, c.x + capR * 0.4f, c.y + capR, false));
    g.fillEllipse(c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawEllipse(c.x - capR, c.y - capR, capR * 2, capR * 2, 1.0f);
    // A faint highlight along the upper left rim of the cap.
    juce::Path hl;
    hl.addCentredArc(c.x, c.y, capR - 1.5f, capR - 1.5f, 0.0f, juce::degreesToRadians(-70.0f), juce::degreesToRadians(15.0f), true);
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.strokePath(hl, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer.
    const juce::Point<float> d(std::sin(a), -std::cos(a));
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawLine({c + d * (capR * 0.18f) + juce::Point<float>(0.0f, 1.0f), c + d * (capR - 2.5f) + juce::Point<float>(0.0f, 1.0f)}, 3.2f);
    g.setColour(t.pointer);
    g.drawLine({c + d * (capR * 0.18f), c + d * (capR - 2.5f)}, 2.4f);
}

void AmpLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) {
    const auto r = b.getLocalBounds().toFloat();
    const float h = std::min(14.0f, r.getHeight() - 2.0f), w = h * 1.9f;
    const auto track = juce::Rectangle<float>(r.getX() + 1.0f, r.getCentreY() - h * 0.5f, w, h);
    const bool on = b.getToggleState();
    const auto& t = theme_;
    g.setColour(on ? t.accent.withAlpha(0.30f) : juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(track, h * 0.5f);
    g.setColour(on ? t.accent : t.edge.brighter(highlighted ? 0.4f : 0.0f));
    g.drawRoundedRectangle(track.reduced(0.5f), h * 0.5f, 1.0f);
    const float kr = h * 0.5f - 2.0f;
    const float kx = on ? track.getRight() - h * 0.5f : track.getX() + h * 0.5f;
    if (on) {
        g.setColour(t.accent.withAlpha(0.35f));
        g.fillEllipse(kx - kr - 2.0f, track.getCentreY() - kr - 2.0f, (kr + 2.0f) * 2, (kr + 2.0f) * 2);
    }
    g.setColour(on ? t.accent.brighter(0.3f) : t.label);
    g.fillEllipse(kx - kr, track.getCentreY() - kr, kr * 2, kr * 2);
    g.setColour(b.findColour(juce::ToggleButton::textColourId));
    g.setFont(font(12.0f));
    g.drawText(b.getButtonText(), juce::Rectangle<float>(track.getRight() + 6.0f, r.getY(), r.getRight() - track.getRight() - 6.0f, r.getHeight()),
               juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ knob

KnobWithLabel::KnobWithLabel(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& caption)
    : attachment_(apvts, paramId, slider_) {
    slider_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14);
    slider_.setDoubleClickReturnValue(true, apvts.getParameter(paramId)->convertFrom0to1(apvts.getParameter(paramId)->getDefaultValue()));
    slider_.setMouseDragSensitivity(160);
    slider_.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider_);
    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(AmpLookAndFeel::font(10.5f, true, 0.1f));
    addAndMakeVisible(caption_);
}

void KnobWithLabel::lookAndFeelChanged() {
    caption_.setColour(juce::Label::textColourId, findColour(juce::Label::textColourId));
}

void KnobWithLabel::resized() {
    auto r = getLocalBounds();
    caption_.setBounds(r.removeFromTop(16));
    slider_.setBounds(r);
}

// ------------------------------------------------------------------ level bar

void LevelBar::setLevel(float peak) {
    const float now = 20.0f * std::log10(std::max(peak, 1e-5f));
    db_ = now > db_ ? now : std::max(now, db_ - 1.6f);
    if (peak >= 0.999f) clip_ = 40;
    else if (clip_ > 0) --clip_;
    repaint();
}

void LevelBar::paint(juce::Graphics& g) {
    const auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGB(14, 12, 10));
    g.fillRoundedRectangle(b, 2.0f);
    const float f = juce::jlimit(0.0f, 1.0f, (db_ + 60.0f) / 60.0f);
    const float h = (b.getHeight() - 2.0f) * f;
    const float top = b.getBottom() - 1.0f - h;
    auto colourAt = [](float db) {
        return db > -6.0f ? juce::Colour::fromRGB(230, 70, 50) : db > -18.0f ? juce::Colour::fromRGB(240, 160, 40) : juce::Colour::fromRGB(80, 200, 100);
    };
    // Drawn in three bands so the colour follows the level.
    const float y18 = b.getBottom() - 1.0f - (b.getHeight() - 2.0f) * (42.0f / 60.0f);
    const float y6 = b.getBottom() - 1.0f - (b.getHeight() - 2.0f) * (54.0f / 60.0f);
    auto band = [&](float yTop, float yBot, juce::Colour col) {
        const float t = std::max(yTop, top);
        if (t < yBot) { g.setColour(col); g.fillRect(b.getX() + 1.0f, t, b.getWidth() - 2.0f, yBot - t); }
    };
    band(y18, b.getBottom() - 1.0f, colourAt(-30.0f));
    band(y6, y18, colourAt(-12.0f));
    band(b.getY() + 1.0f, y6, colourAt(-3.0f));
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawHorizontalLine(int(y18), b.getX(), b.getRight());
    g.drawHorizontalLine(int(y6), b.getX(), b.getRight());
    if (clip_ > 0) {
        g.setColour(juce::Colour::fromRGB(230, 70, 50));
        g.fillRect(b.getX() + 1.0f, b.getY() + 1.0f, b.getWidth() - 2.0f, 3.0f);
    }
}

// ------------------------------------------------------------------ editor

SasEditor::SasEditor(SasProcessor& p)
    : AudioProcessorEditor(&p), processor_(p),
      input_(p.apvts, ids::input, "INPUT dB"), output_(p.apvts, ids::output, "OUTPUT dB"),
      tsDrive_(p.apvts, ids::tsDrive, "DRIVE"), tsTone_(p.apvts, ids::tsTone, "TONE"), tsLevel_(p.apvts, ids::tsLevel, "LEVEL"),
      gateThreshold_(p.apvts, ids::gateThreshold, "THRESHOLD"), gateRelease_(p.apvts, ids::gateRelease, "RELEASE"), cable_(p.apvts, ids::cable, "CABLE m"),
      bias_(p.apvts, ids::bias, "BIAS"), wear_(p.apvts, ids::wear, "WEAR") {
    setLookAndFeel(&look_);
    for (auto* k : {&input_, &output_, &tsDrive_, &tsTone_, &tsLevel_, &gateThreshold_, &gateRelease_, &cable_, &bias_, &wear_}) addAndMakeVisible(*k);
    addAndMakeVisible(inBar_);
    addAndMakeVisible(outBar_);

    if (const auto* amp = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(ids::amp))) ampNames_ = amp->choices;
    ampList_.setModel(this);
    ampList_.setRowHeight(26);
    addAndMakeVisible(ampList_);

    styleCombo(quality_, ids::quality, qualityAtt_);
    styleCombo(cab_, ids::cab, cabAtt_);
    styleCombo(pickup_, ids::pickup, pickupAtt_);
    styleCaption(pickupCaption_, "PICKUP");
    styleCombo(noise_, ids::noise, noiseAtt_);
    styleCombo(unit_, ids::unit, unitAtt_);
    styleCaption(noiseCaption_, "NOISE AND HUM");
    styleCaption(unitCaption_, "UNIT");
    styleCaption(tubeCaption_, "POWER TUBES");
    addAndMakeVisible(tube_);
    tube_.onChange = [this] {
        if (auto* param = processor_.apvts.getParameter(ids::tube)) {
            const int index = tube_.getSelectedItemIndex();
            if (index < 0) return;
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(float(index)));
            param->endChangeGesture();
        }
    };
    idleLabel_.setFont(AmpLookAndFeel::font(11.5f));
    addAndMakeVisible(idleLabel_);
    conditionNote_.setText("Tubes, wear and unit rebuild the circuits; they act in Normal and High quality.", juce::dontSendNotification);
    conditionNote_.setFont(AmpLookAndFeel::font(10.5f));
    addAndMakeVisible(conditionNote_);
    // The jack list depends on the amp, so it is filled by showAmp() and wired by hand.
    addAndMakeVisible(jack_);
    jack_.onChange = [this] {
        if (auto* param = processor_.apvts.getParameter(ids::jack)) {
            const int index = jack_.getSelectedItemIndex();
            if (index < 0) return;
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(float(index)));
            param->endChangeGesture();
        }
    };
    styleCaption(ampCaption_, "AMP");
    styleCaption(qualityCaption_, "QUALITY");
    styleCaption(jackCaption_, "INPUT JACK");
    styleCaption(cabCaption_, "CABINET");
    addAndMakeVisible(tsOn_);
    tsOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tsOn, tsOn_);
    addAndMakeVisible(gateOn_);
    gateOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::gateOn, gateOn_);
    addAndMakeVisible(vibrato_);
    vibratoAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::vibOn, vibrato_);
    addAndMakeVisible(levelMatch_);
    levelMatch_.setTooltip("Raises the quieter jack so both play at a similar level");
    levelMatchAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::levelMatch, levelMatch_);

    irName_.setFont(AmpLookAndFeel::font(11.0f));
    addAndMakeVisible(irName_);
    addAndMakeVisible(loadIr_);
    addAndMakeVisible(clearIr_);
    loadIr_.onClick = [this] { chooseImpulseResponse(); };
    clearIr_.onClick = [this] { processor_.clearImpulseResponse(); };

    for (auto* t : {&ampTab_, &fxTab_}) {
        t->setClickingTogglesState(true);
        t->setRadioGroupId(7);
        addAndMakeVisible(*t);
    }
    ampTab_.setToggleState(true, juce::dontSendNotification);
    ampTab_.onClick = [this] { setPage(0); };
    fxTab_.onClick = [this] { setPage(1); };

    showAmp(processor_.selectedAmpIndex());
    setResizable(true, true);
    setResizeLimits(680, 380, 1200, 760);
    setSize(800, 410);
    startTimerHz(30);
    timerCallback();
}

SasEditor::~SasEditor() {
    ampList_.setModel(nullptr);
    setLookAndFeel(nullptr);
}

// ------------------------------------------------------------------ amp list

int SasEditor::getNumRows() { return ampNames_.size(); }

void SasEditor::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= ampNames_.size()) return;
    if (selected) {
        g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.26f), 0.0f, 0.0f, look_.accent().withAlpha(0.06f), float(w), 0.0f, false));
        g.fillRect(0, 0, w, h);
        g.setColour(look_.accent());
        g.fillRoundedRectangle(0.0f, 4.0f, 3.0f, float(h - 8), 1.5f);
    }
    g.setColour(selected ? look_.accent() : look_.text().withAlpha(0.8f));
    g.setFont(AmpLookAndFeel::font(14.0f, selected, 0.01f));
    g.drawText(ampNames_[row], 12, 0, w - 16, h, juce::Justification::centredLeft);
}

void SasEditor::selectedRowsChanged(int lastRow) {
    if (lastRow < 0 || lastRow == processor_.selectedAmpIndex()) return;
    if (auto* param = processor_.apvts.getParameter(ids::amp)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(float(lastRow)));
        param->endChangeGesture();
    }
}

void SasEditor::styleCombo(juce::ComboBox& box, const char* paramId,
                           std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att) {
    addAndMakeVisible(box);
    // Items first, then the attachment selects the stored one.
    const auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor_.apvts.getParameter(paramId));
    box.addItemList(param->choices, 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor_.apvts, paramId, box);
}

void SasEditor::styleCaption(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
    l.setColour(juce::Label::textColourId, look_.label());
    addAndMakeVisible(l);
}

void SasEditor::applyTheme(const sas::AmpDef& def) {
    look_.setTheme(EditorTheme::from(def.theme));
    for (auto* l : {&ampCaption_, &qualityCaption_, &jackCaption_, &cabCaption_, &pickupCaption_, &tubeCaption_, &noiseCaption_, &unitCaption_, &conditionNote_})
        l->setColour(juce::Label::textColourId, look_.label());
    irName_.setColour(juce::Label::textColourId, look_.label());
    sendLookAndFeelChange();
    repaint();
}

void SasEditor::setPage(int page) {
    page_ = page;
    resized();
}

void SasEditor::showAmp(int index) {
    const auto& defs = processor_.amps();
    if (index < 0 || index >= int(defs.size())) return;
    shownAmp_ = index;
    const auto& def = defs[std::size_t(index)];
    title_ = juce::String(def.name);
    applyTheme(def);

    ampKnobs_.clear();
    for (const auto& u : def.ui) {
        auto k = std::make_unique<KnobWithLabel>(processor_.apvts, u.param.c_str(), u.label);
        addAndMakeVisible(*k);
        ampKnobs_.push_back(std::move(k));
    }

    jack_.clear(juce::dontSendNotification);
    for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) jack_.addItem(juce::String(def.preamp.jacks[j].label), int(j) + 1);
    jackCaption_.setText(juce::String(def.preamp.jackLabel), juce::dontSendNotification);
    tube_.clear(juce::dontSendNotification);
    for (std::size_t t = 0; t < def.power.tubes.size(); ++t) tube_.addItem(juce::String(def.power.tubes[t].name), int(t) + 1);
    tube_.setSelectedItemIndex(juce::jlimit(0, std::max(0, int(def.power.tubes.size()) - 1), int(processor_.apvts.getRawParameterValue(ids::tube)->load() + 0.5f)), juce::dontSendNotification);
    tube_.setEnabled(def.power.tubes.size() > 1);
    bias_.setEnabled(def.power.bias.adjustable());
    bias_.setAlpha(def.power.bias.adjustable() ? 1.0f : 0.4f);
    vibrato_.setVisible(def.preamp.vibrato.present);
    levelMatch_.setVisible(std::any_of(def.preamp.jacks.begin(), def.preamp.jacks.end(), [](const sas::JackDef& j) { return j.levelDb != 0.0; }));
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    jack_.setSelectedItemIndex(juce::jlimit(0, int(def.preamp.jacks.size()) - 1, jackNow), juce::dontSendNotification);
    {
        juce::SparseSet<int> sel;
        sel.addRange(juce::Range<int>(index, index + 1));
        ampList_.setSelectedRows(sel, juce::dontSendNotification);
    }
    ampList_.repaint();
    resized();
    repaint();
}

void SasEditor::pollMeters() {
    inBar_.setLevel(processor_.takeInputPeak());
    outBar_.setLevel(processor_.takeOutputPeak());
}

void SasEditor::timerCallback() {
    pollMeters();
    if (tick_++ % 8 != 0) return;  // the rest at about 4 Hz
    // Follows the amp parameter when the host or a restored state changes it.
    if (processor_.selectedAmpIndex() != shownAmp_) showAmp(processor_.selectedAmpIndex());
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    if (jackNow < jack_.getNumItems() && jack_.getSelectedItemIndex() != jackNow)
        jack_.setSelectedItemIndex(jackNow, juce::dontSendNotification);

    if (const int tubeNow = int(processor_.apvts.getRawParameterValue(ids::tube)->load() + 0.5f);
        tubeNow < tube_.getNumItems() && tube_.getSelectedItemIndex() != tubeNow)
        tube_.setSelectedItemIndex(tubeNow, juce::dontSendNotification);
    {
        const float ma = processor_.idleMilliamps();
        idleLabel_.setText(ma > 0.5f ? juce::String(ma, 1) + " mA per tube" : juce::String("plate current: Normal / High only"), juce::dontSendNotification);
    }

    const auto name = processor_.impulseResponseName();
    irName_.setText(name.isEmpty() ? "no IR loaded (cab filter)" : name, juce::dontSendNotification);
}

void SasEditor::chooseImpulseResponse() {
    chooser_ = std::make_unique<juce::FileChooser>("Choose a cabinet impulse response", juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File{}) return;
                              if (processor_.loadImpulseResponse(file)) {
                                  // Switch the cabinet to IR so the load is audible right away.
                                  if (auto* cab = processor_.apvts.getParameter(ids::cab)) cab->setValueNotifyingHost(cab->convertTo0to1(2.0f));
                              } else {
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim",
                                                                         "Could not read that file as an impulse response.");
                              }
                          });
}

void SasEditor::paint(juce::Graphics& g) {
    const auto bg = look_.background();
    g.setGradientFill(juce::ColourGradient(bg.brighter(0.18f), 0.0f, 0.0f, bg.darker(0.35f), 0.0f, float(getHeight()), false));
    g.fillAll();
    // A soft glow of the accent colour behind the title.
    g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.07f), float(getWidth()) * 0.35f, 0.0f, juce::Colours::transparentBlack,
                                           float(getWidth()) * 0.35f, 120.0f, true));
    g.fillRect(0, 0, getWidth(), 130);

    for (const auto& p : panels_) {
        const auto b = p.bounds.toFloat();
        const auto top = look_.panel().brighter(0.10f);
        g.setGradientFill(juce::ColourGradient(top, 0.0f, b.getY(), look_.panel().darker(0.15f), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle(b, 7.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawHorizontalLine(int(b.getY()) + 1, b.getX() + 8.0f, b.getRight() - 8.0f);
        g.setColour(look_.panelEdge());
        g.drawRoundedRectangle(b.reduced(0.5f), 7.0f, 1.0f);
        if (p.caption.isNotEmpty()) {
            g.setColour(look_.accent());
            g.fillRoundedRectangle(b.getX() + 10.0f, b.getY() + 9.0f, 3.0f, 9.0f, 1.5f);
            g.setColour(look_.label());
            g.setFont(AmpLookAndFeel::font(10.0f, true, 0.07f));
            g.drawText(p.caption, p.bounds.getX() + 18, p.bounds.getY() + 4, p.bounds.getWidth() - 28, 18, juce::Justification::centredLeft);
        }
    }

    // Title of the amp, with the product name small above it, and a thin accent line under both.
    const int left = 12 + 176;
    g.setColour(look_.label());
    g.setFont(AmpLookAndFeel::font(10.0f, true, 0.30f));
    g.drawText("SCHEMATIC AMP SIM", left, 8, 300, 14, juce::Justification::centredLeft);
    g.setColour(look_.accent());
    g.setFont(AmpLookAndFeel::font(24.0f, true, 0.02f));
    g.drawText(title_, left, 20, getWidth() - left - 12, 30, juce::Justification::centredLeft);
    g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.7f), float(left), 0.0f, look_.accent().withAlpha(0.0f), float(getWidth() - 12), 0.0f, false));
    g.fillRect(float(left), 49.0f, float(getWidth() - 12 - left), 1.0f);
}

void SasEditor::resized() {
    constexpr int pad = 12, gap = 8, leftW = 168;
    panels_.clear();
    auto area = getLocalBounds().reduced(pad);

    // Left column: the amp list, jack, quality.
    {
        auto col = area.removeFromLeft(leftW);
        area.removeFromLeft(gap);
        ampCaption_.setBounds(col.removeFromTop(16));
        const int rows = std::max(1, ampNames_.size());
        ampList_.setBounds(col.removeFromTop(rows * 26 + 2));
        col.removeFromTop(gap);
        jackCaption_.setBounds(col.removeFromTop(16));
        jack_.setBounds(col.removeFromTop(24));
        col.removeFromTop(4);
        if (levelMatch_.isVisible()) levelMatch_.setBounds(col.removeFromTop(22));
        if (vibrato_.isVisible()) vibrato_.setBounds(col.removeFromTop(22));
        col.removeFromTop(4);
        qualityCaption_.setBounds(col.removeFromTop(16));
        quality_.setBounds(col.removeFromTop(24));
    }

    // Right: title and page tabs, then the page.
    {
        auto head = area.removeFromTop(36);
        fxTab_.setBounds(head.removeFromRight(80).withTrimmedTop(6).withTrimmedBottom(2));
        head.removeFromRight(4);
        ampTab_.setBounds(head.removeFromRight(70).withTrimmedTop(6).withTrimmedBottom(2));
        area.removeFromTop(4);
    }
    const bool ampPage = page_ == 0;
    for (auto& k : ampKnobs_) k->setVisible(ampPage);
    for (auto* c : std::initializer_list<juce::Component*>{&cabCaption_, &cab_, &loadIr_, &clearIr_, &irName_, &input_, &output_, &inBar_, &outBar_}) c->setVisible(ampPage);
    for (auto* c : std::initializer_list<juce::Component*>{&tsOn_, &tsDrive_, &tsTone_, &tsLevel_, &gateOn_, &gateThreshold_, &gateRelease_, &cable_, &pickup_, &pickupCaption_,
                                                           &bias_, &wear_, &tube_, &noise_, &unit_, &tubeCaption_, &noiseCaption_, &unitCaption_, &idleLabel_, &conditionNote_}) c->setVisible(!ampPage);

    if (!ampPage) {
        // Effects page: one panel per effect, side by side; only the overdrive for now.
        const int knobW = std::clamp(area.getWidth() / 8, 70, 110);
        auto fxRow = area.removeFromTop(std::min(area.getHeight(), 190));
        auto tsPanel = fxRow.removeFromLeft(knobW * 3 + 12);
        fxRow.removeFromLeft(gap);
        auto gatePanel = fxRow.removeFromLeft(knobW * 2 + 12);
        panels_.push_back({gatePanel, "NOISE GATE"});
        {
            auto gin = gatePanel.reduced(6, 4);
            auto ghead = gin.removeFromTop(20);
            gateOn_.setBounds(ghead.removeFromRight(56));
            gateThreshold_.setBounds(gin.removeFromLeft(gin.getWidth() / 2));
            gateRelease_.setBounds(gin);
        }
        fxRow.removeFromLeft(gap);
        auto guitarPanel = fxRow.removeFromLeft(knobW * 2 + 12);
        panels_.push_back({guitarPanel, "GUITAR INPUT"});
        {
            auto gin = guitarPanel.reduced(8, 4);
            gin.removeFromTop(18);
            pickupCaption_.setBounds(gin.removeFromTop(16));
            pickup_.setBounds(gin.removeFromTop(24));
            cable_.setBounds(gin.removeFromLeft(gin.getWidth() / 2));
        }
        // Second row: the state of the amp itself.
        area.removeFromTop(gap);
        auto condPanel = area.removeFromTop(std::min(area.getHeight(), 150));
        panels_.push_back({condPanel, "AMP CONDITION"});
        {
            auto cin = condPanel.reduced(8, 4);
            cin.removeFromTop(18);
            conditionNote_.setBounds(cin.removeFromBottom(16));
            auto col1 = cin.removeFromLeft(130);
            tubeCaption_.setBounds(col1.removeFromTop(16));
            tube_.setBounds(col1.removeFromTop(24));
            idleLabel_.setBounds(col1.removeFromTop(20));
            bias_.setBounds(cin.removeFromLeft(knobW));
            auto col2 = cin.removeFromLeft(120);
            col2.removeFromLeft(10);
            noiseCaption_.setBounds(col2.removeFromTop(16));
            noise_.setBounds(col2.removeFromTop(24));
            wear_.setBounds(cin.removeFromLeft(knobW));
            auto col3 = cin.removeFromLeft(110);
            unitCaption_.setBounds(col3.removeFromTop(16));
            unit_.setBounds(col3.removeFromTop(24));
        }
        panels_.push_back({tsPanel, "OVERDRIVE - TS808 STYLE"});
        auto in = tsPanel.reduced(6, 4);
        auto head = in.removeFromTop(20);
        tsOn_.setBounds(head.removeFromRight(56));
        tsDrive_.setBounds(in.removeFromLeft(in.getWidth() / 3));
        tsTone_.setBounds(in.removeFromLeft(in.getWidth() / 2));
        tsLevel_.setBounds(in);
        repaint();
        return;
    }

    const int bottomH = std::clamp(area.getHeight() * 38 / 100, 110, 160);
    auto bottom = area.removeFromBottom(bottomH);
    area.removeFromBottom(gap);

    panels_.push_back({area, "AMP CONTROLS"});
    {
        auto row = area.reduced(6, 4);
        row.removeFromTop(16);
        const int n = std::max<int>(1, int(ampKnobs_.size()));
        const int w = row.getWidth() / n;
        for (auto& k : ampKnobs_) k->setBounds(row.removeFromLeft(w));
    }

    const int knobW = std::clamp(bottom.getWidth() / 7, 64, 100);
    auto ioPanel = bottom.removeFromRight(knobW * 2 + 12 + 2 * 16);
    bottom.removeFromRight(gap);
    auto cabPanel = bottom;
    panels_.push_back({cabPanel, ""});
    {
        auto in = cabPanel.reduced(10, 6);
        cabCaption_.setBounds(in.removeFromTop(16));
        cab_.setBounds(in.removeFromTop(24).removeFromLeft(std::min(220, in.getWidth())));
        in.removeFromTop(6);
        auto btns = in.removeFromTop(24);
        loadIr_.setBounds(btns.removeFromLeft(96));
        btns.removeFromLeft(6);
        clearIr_.setBounds(btns.removeFromLeft(64));
        in.removeFromTop(4);
        irName_.setBounds(in.removeFromTop(18));
    }
    panels_.push_back({ioPanel, "LEVELS"});
    {
        auto in = ioPanel.reduced(6, 4);
        in.removeFromTop(16);
        auto col = [&](KnobWithLabel& k, LevelBar& bar, juce::Rectangle<int> r) {
            bar.setBounds(r.removeFromRight(12).withTrimmedTop(20).withTrimmedBottom(20));
            k.setBounds(r);
        };
        col(input_, inBar_, in.removeFromLeft(in.getWidth() / 2));
        col(output_, outBar_, in);
    }
    repaint();
}
