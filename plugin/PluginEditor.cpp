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
      volume_(p.apvts, ids::volume, "PREAMP"), bass_(p.apvts, ids::bass, "BASS"), middle_(p.apvts, ids::middle, "MIDDLE"),
      treble_(p.apvts, ids::treble, "TREBLE"), presence_(p.apvts, ids::presence, "PRESENCE"),
      master_(p.apvts, ids::master, "MASTER"), input_(p.apvts, ids::input, "INPUT dB"), output_(p.apvts, ids::output, "OUTPUT dB"),
      tsDrive_(p.apvts, ids::tsDrive, "DRIVE"), tsTone_(p.apvts, ids::tsTone, "TONE"), tsLevel_(p.apvts, ids::tsLevel, "LEVEL") {
    setLookAndFeel(&look_);
    for (auto* k : {&volume_, &bass_, &middle_, &treble_, &presence_, &master_, &input_, &output_, &tsDrive_, &tsTone_, &tsLevel_})
        addAndMakeVisible(*k);

    styleCombo(quality_, ids::quality, qualityAtt_);
    styleCombo(jack_, ids::jack, jackAtt_);
    styleCombo(cab_, ids::cab, cabAtt_);
    styleCaption(qualityCaption_, "QUALITY");
    styleCaption(jackCaption_, "INPUT JACK");
    styleCaption(cabCaption_, "CABINET");
    styleCaption(tsCaption_, "TUBE SCREAMER");
    addAndMakeVisible(tsOn_);
    tsOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tsOn, tsOn_);

    irName_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    irName_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(irName_);
    addAndMakeVisible(loadIr_);
    addAndMakeVisible(clearIr_);
    loadIr_.onClick = [this] { chooseImpulseResponse(); };
    clearIr_.onClick = [this] { processor_.clearImpulseResponse(); };

    setResizable(true, true);
    setResizeLimits(560, 480, 1100, 900);
    setSize(660, 520);
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

void AesEditor::timerCallback() {
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
    g.drawText("JCM800  2203", getLocalBounds().removeFromTop(48), juce::Justification::centred);
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

    // Row 1: the six amp knobs.
    {
        auto row = area.removeFromTop(rowH);
        const int w = row.getWidth() / 6;
        for (auto* k : {&volume_, &bass_, &middle_, &treble_, &presence_, &master_}) k->setBounds(row.removeFromLeft(w).reduced(2));
    }
    // Row 2: input/output trims and the selectors.
    {
        auto row = area.removeFromTop(rowH);
        const int w = row.getWidth() / 6;
        input_.setBounds(row.removeFromLeft(w).reduced(2));
        output_.setBounds(row.removeFromLeft(w).reduced(2));
        row.removeFromLeft(w / 3);
        auto col = row.reduced(4, 8);
        const int h = col.getHeight() / 3;
        auto line = [&](juce::Label& cap, juce::ComboBox& box) {
            auto r = col.removeFromTop(h);
            cap.setBounds(r.removeFromTop(18));
            box.setBounds(r.removeFromTop(26).removeFromLeft(std::min(220, r.getWidth())));
        };
        line(qualityCaption_, quality_);
        line(jackCaption_, jack_);
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
