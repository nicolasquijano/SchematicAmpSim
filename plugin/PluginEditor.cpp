#include "PluginEditor.h"

namespace {
const juce::Colour kBackground = juce::Colour::fromRGB(18, 16, 14);
const juce::Colour kBody = juce::Colour::fromRGB(24, 24, 24);
}  // namespace

// ------------------------------------------------------------------ look

AmpLookAndFeel::AmpLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, kBody);
    setColour(juce::ComboBox::outlineColourId, gold().withAlpha(0.6f));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, gold());
    setColour(juce::PopupMenu::backgroundColourId, kBody);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, gold().withAlpha(0.35f));
    setColour(juce::TextButton::buttonColourId, kBody);
    setColour(juce::TextButton::textColourOffId, gold());
    setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    setColour(juce::ToggleButton::tickColourId, gold());
}

void AmpLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                      float endAngle, juce::Slider&) {
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(8.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float bodyRadius = radius * 0.78f;

    g.setColour(gold());
    for (int i = 0; i <= 10; ++i) {  // tick marks like the dial on the amp
        const float a = startAngle + (endAngle - startAngle) * float(i) / 10.0f;
        const juce::Point<float> d(std::sin(a), -std::cos(a));
        g.drawLine({centre + d * (bodyRadius + 4.0f), centre + d * (bodyRadius + 9.0f)}, 1.0f);
    }
    g.setColour(kBody);
    g.fillEllipse(centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2, bodyRadius * 2);
    g.setColour(gold());
    g.drawEllipse(centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2, bodyRadius * 2, 2.0f);

    const float a = startAngle + (endAngle - startAngle) * pos;
    const juce::Point<float> d(std::sin(a), -std::cos(a));
    g.setColour(juce::Colours::white);
    g.drawLine({centre + d * 6.0f, centre + d * (bodyRadius - 4.0f)}, 3.5f);
}

// ------------------------------------------------------------------ knob

KnobWithLabel::KnobWithLabel(juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& caption)
    : attachment_(apvts, paramId, slider_) {
    slider_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    slider_.setDoubleClickReturnValue(true, apvts.getParameter(paramId)->convertFrom0to1(apvts.getParameter(paramId)->getDefaultValue()));
    slider_.setMouseDragSensitivity(160);
    addAndMakeVisible(slider_);
    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    caption_.setColour(juce::Label::textColourId, AmpLookAndFeel::gold());
    addAndMakeVisible(caption_);
}

void KnobWithLabel::resized() {
    auto r = getLocalBounds();
    caption_.setBounds(r.removeFromTop(18));
    slider_.setBounds(r);
}

// ------------------------------------------------------------------ editor

AesEditor::AesEditor(AesProcessor& p)
    : AudioProcessorEditor(&p), processor_(p),
      input_(p.apvts, ids::input, "INPUT dB"), output_(p.apvts, ids::output, "OUTPUT dB"),
      tsDrive_(p.apvts, ids::tsDrive, "DRIVE"), tsTone_(p.apvts, ids::tsTone, "TONE"), tsLevel_(p.apvts, ids::tsLevel, "LEVEL") {
    setLookAndFeel(&look_);
    for (auto* k : {&input_, &output_, &tsDrive_, &tsTone_, &tsLevel_}) addAndMakeVisible(*k);

    styleCombo(amp_, ids::amp, ampAtt_);
    styleCombo(quality_, ids::quality, qualityAtt_);
    styleCombo(cab_, ids::cab, cabAtt_);
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
    styleCaption(tsCaption_, "TUBE SCREAMER");
    addAndMakeVisible(tsOn_);
    tsOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tsOn, tsOn_);
    addAndMakeVisible(levelMatch_);
    levelMatch_.setTooltip("Raises the quieter jack so both play at a similar level");
    levelMatchAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::levelMatch, levelMatch_);

    irName_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    irName_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(irName_);
    addAndMakeVisible(loadIr_);
    addAndMakeVisible(clearIr_);
    loadIr_.onClick = [this] { chooseImpulseResponse(); };
    clearIr_.onClick = [this] { processor_.clearImpulseResponse(); };

    showAmp(processor_.selectedAmpIndex());
    setResizable(true, true);
    setResizeLimits(560, 480, 1100, 900);
    setSize(660, 540);
    startTimerHz(4);
    timerCallback();
}

AesEditor::~AesEditor() { setLookAndFeel(nullptr); }

void AesEditor::styleCombo(juce::ComboBox& box, const char* paramId,
                           std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att) {
    addAndMakeVisible(box);
    // Items first, then the attachment selects the stored one.
    const auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor_.apvts.getParameter(paramId));
    box.addItemList(param->choices, 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor_.apvts, paramId, box);
}

void AesEditor::styleCaption(juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, AmpLookAndFeel::gold());
    addAndMakeVisible(l);
}

void AesEditor::showAmp(int index) {
    const auto& defs = processor_.amps();
    if (index < 0 || index >= int(defs.size())) return;
    shownAmp_ = index;
    const auto& def = defs[std::size_t(index)];
    title_ = juce::String(def.name).toUpperCase();

    ampKnobs_.clear();
    for (const auto& u : def.ui) {
        auto k = std::make_unique<KnobWithLabel>(processor_.apvts, u.param.c_str(), u.label);
        addAndMakeVisible(*k);
        ampKnobs_.push_back(std::move(k));
    }

    jack_.clear(juce::dontSendNotification);
    for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) jack_.addItem(juce::String(def.preamp.jacks[j].label), int(j) + 1);
    jackCaption_.setText(juce::String(def.preamp.jackLabel), juce::dontSendNotification);
    levelMatch_.setVisible(std::any_of(def.preamp.jacks.begin(), def.preamp.jacks.end(), [](const aes::JackDef& j) { return j.levelDb != 0.0; }));
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    jack_.setSelectedItemIndex(juce::jlimit(0, int(def.preamp.jacks.size()) - 1, jackNow), juce::dontSendNotification);
    resized();
    repaint();
}

void AesEditor::timerCallback() {
    // Follows the amp parameter when the host or a restored state changes it.
    if (processor_.selectedAmpIndex() != shownAmp_) showAmp(processor_.selectedAmpIndex());
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    if (jackNow < jack_.getNumItems() && jack_.getSelectedItemIndex() != jackNow)
        jack_.setSelectedItemIndex(jackNow, juce::dontSendNotification);

    const auto name = processor_.impulseResponseName();
    irName_.setText(name.isEmpty() ? "no IR loaded (the cab filter is used)" : name, juce::dontSendNotification);
}

void AesEditor::chooseImpulseResponse() {
    chooser_ = std::make_unique<juce::FileChooser>("Choose a cabinet impulse response", juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File{}) return;
                              if (processor_.loadImpulseResponse(file)) {
                                  // Switch the cabinet to IR so the load is audible right away.
                                  if (auto* cab = processor_.apvts.getParameter(ids::cab)) cab->setValueNotifyingHost(cab->convertTo0to1(2.0f));
                              } else {
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "AES",
                                                                         "Could not read that file as an impulse response.");
                              }
                          });
}

void AesEditor::paint(juce::Graphics& g) {
    g.fillAll(kBackground);
    g.setColour(AmpLookAndFeel::gold());
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawText(title_, getLocalBounds().removeFromTop(48), juce::Justification::centred);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("AES - amp schematic simulator", getLocalBounds().removeFromTop(66).removeFromBottom(18), juce::Justification::centred);

    // Section dividers.
    g.setColour(AmpLookAndFeel::gold().withAlpha(0.25f));
    const auto r = getLocalBounds().reduced(14);
    const int rowH = (r.getHeight() - 70) / 3;
    g.drawHorizontalLine(70 + rowH, float(r.getX()), float(r.getRight()));
    g.drawHorizontalLine(70 + rowH * 2, float(r.getX()), float(r.getRight()));
}

void AesEditor::resized() {
    auto area = getLocalBounds().reduced(14);
    area.removeFromTop(70);
    const int rowH = area.getHeight() / 3;

    // Row 1: the amp's own knobs.
    {
        auto row = area.removeFromTop(rowH);
        const int n = std::max<int>(1, int(ampKnobs_.size()));
        const int w = row.getWidth() / n;
        for (auto& k : ampKnobs_) k->setBounds(row.removeFromLeft(w).reduced(2));
    }
    // Row 2: input/output trims and the selectors.
    {
        auto row = area.removeFromTop(rowH);
        const int w = row.getWidth() / 6;
        input_.setBounds(row.removeFromLeft(w).reduced(2));
        output_.setBounds(row.removeFromLeft(w).reduced(2));
        row.removeFromLeft(w / 3);
        auto col = row.reduced(4, 4);
        const int h = col.getHeight() / 4;
        auto line = [&](juce::Label& cap, juce::ComboBox& box) {
            auto r = col.removeFromTop(h);
            cap.setBounds(r.removeFromTop(16));
            box.setBounds(r.removeFromTop(24).removeFromLeft(std::min(220, r.getWidth())));
        };
        line(ampCaption_, amp_);
        line(qualityCaption_, quality_);
        line(jackCaption_, jack_);
        levelMatch_.setBounds(jack_.getRight() + 10, jack_.getY(), 120, 24);
        line(cabCaption_, cab_);
    }
    // Row 3: Tube Screamer and the IR loader.
    {
        auto row = area;
        const int w = row.getWidth() / 6;
        auto ts = row.removeFromLeft(w * 2 - 10).reduced(4, 8);
        tsCaption_.setBounds(ts.removeFromTop(18));
        tsOn_.setBounds(ts.removeFromTop(26));
        tsDrive_.setBounds(row.removeFromLeft(w).reduced(2));
        tsTone_.setBounds(row.removeFromLeft(w).reduced(2));
        tsLevel_.setBounds(row.removeFromLeft(w).reduced(2));
        auto ir = row.reduced(4, 20);
        loadIr_.setBounds(ir.removeFromTop(26).removeFromLeft(100));
        ir.removeFromTop(6);
        clearIr_.setBounds(ir.removeFromTop(26).removeFromLeft(100));
        ir.removeFromTop(4);
        irName_.setBounds(ir.removeFromTop(36).withWidth(row.getWidth()));
    }
}
