#include "PluginEditor.h"

namespace {
const juce::Colour kBackground = juce::Colour::fromRGB(15, 13, 11);
const juce::Colour kBody = juce::Colour::fromRGB(34, 29, 24);
}  // namespace

// ------------------------------------------------------------------ look

AmpLookAndFeel::AmpLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, kBackground);
    setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, accent().withAlpha(0.4f));
    setColour(juce::ComboBox::backgroundColourId, kBody);
    setColour(juce::ComboBox::outlineColourId, panelEdge());
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::arrowColourId, accent());
    setColour(juce::PopupMenu::backgroundColourId, kBody);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent().withAlpha(0.35f));
    setColour(juce::TextButton::buttonColourId, kBody);
    setColour(juce::TextButton::textColourOffId, accent());
    setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    setColour(juce::ToggleButton::tickColourId, accent());
    setColour(juce::ListBox::backgroundColourId, panel());
    setColour(juce::ListBox::outlineColourId, panelEdge());
}

void AmpLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                      float endAngle, juce::Slider&) {
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(4.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float arcR = radius - 2.0f;
    const float bodyR = radius * 0.70f;
    const float a = startAngle + (endAngle - startAngle) * pos;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour::fromRGB(58, 49, 40));
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, a, true);
    g.setColour(AmpLookAndFeel::accent());
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setGradientFill(juce::ColourGradient(juce::Colour::fromRGB(58, 50, 42), centre.x, centre.y - bodyR,
                                           juce::Colour::fromRGB(22, 19, 16), centre.x, centre.y + bodyR, false));
    g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour(panelEdge());
    g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2, bodyR * 2, 1.5f);

    const juce::Point<float> d(std::sin(a), -std::cos(a));
    g.setColour(juce::Colours::white);
    g.drawLine({centre + d * (bodyR * 0.30f), centre + d * (bodyR - 3.0f)}, 2.5f);
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
    slider_.setColour(juce::Slider::textBoxTextColourId, juce::Colours::lightgrey);
    addAndMakeVisible(slider_);
    caption_.setText(caption, juce::dontSendNotification);
    caption_.setJustificationType(juce::Justification::centred);
    caption_.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    caption_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(caption_);
}

void KnobWithLabel::resized() {
    auto r = getLocalBounds();
    caption_.setBounds(r.removeFromTop(16));
    slider_.setBounds(r);
}

// ------------------------------------------------------------------ editor

SasEditor::SasEditor(SasProcessor& p)
    : AudioProcessorEditor(&p), processor_(p),
      input_(p.apvts, ids::input, "INPUT dB"), output_(p.apvts, ids::output, "OUTPUT dB"),
      tsDrive_(p.apvts, ids::tsDrive, "DRIVE"), tsTone_(p.apvts, ids::tsTone, "TONE"), tsLevel_(p.apvts, ids::tsLevel, "LEVEL") {
    setLookAndFeel(&look_);
    for (auto* k : {&input_, &output_, &tsDrive_, &tsTone_, &tsLevel_}) addAndMakeVisible(*k);

    if (const auto* amp = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(ids::amp))) ampNames_ = amp->choices;
    ampList_.setModel(this);
    ampList_.setRowHeight(26);
    addAndMakeVisible(ampList_);

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
    addAndMakeVisible(tsOn_);
    tsOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tsOn, tsOn_);
    addAndMakeVisible(vibrato_);
    vibratoAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::vibOn, vibrato_);
    addAndMakeVisible(levelMatch_);
    levelMatch_.setTooltip("Raises the quieter jack so both play at a similar level");
    levelMatchAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::levelMatch, levelMatch_);

    irName_.setColour(juce::Label::textColourId, juce::Colours::grey);
    irName_.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(irName_);
    addAndMakeVisible(loadIr_);
    addAndMakeVisible(clearIr_);
    loadIr_.onClick = [this] { chooseImpulseResponse(); };
    clearIr_.onClick = [this] { processor_.clearImpulseResponse(); };

    for (auto* t : {&ampTab_, &fxTab_}) {
        t->setClickingTogglesState(true);
        t->setRadioGroupId(7);
        t->setColour(juce::TextButton::buttonOnColourId, AmpLookAndFeel::accent().withAlpha(0.25f));
        t->setColour(juce::TextButton::textColourOnId, AmpLookAndFeel::accent());
        t->setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
        addAndMakeVisible(*t);
    }
    ampTab_.setToggleState(true, juce::dontSendNotification);
    ampTab_.onClick = [this] { setPage(0); };
    fxTab_.onClick = [this] { setPage(1); };

    showAmp(processor_.selectedAmpIndex());
    setResizable(true, true);
    setResizeLimits(680, 380, 1200, 760);
    setSize(800, 410);
    startTimerHz(4);
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
        g.setColour(AmpLookAndFeel::accent().withAlpha(0.18f));
        g.fillRect(0, 0, w, h);
        g.setColour(AmpLookAndFeel::accent());
        g.fillRect(0, 3, 3, h - 6);
    }
    g.setColour(selected ? AmpLookAndFeel::accent() : juce::Colours::lightgrey);
    g.setFont(juce::FontOptions(14.0f, selected ? juce::Font::bold : juce::Font::plain));
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
    l.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    l.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 128, 98));
    addAndMakeVisible(l);
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

    ampKnobs_.clear();
    for (const auto& u : def.ui) {
        auto k = std::make_unique<KnobWithLabel>(processor_.apvts, u.param.c_str(), u.label);
        addAndMakeVisible(*k);
        ampKnobs_.push_back(std::move(k));
    }

    jack_.clear(juce::dontSendNotification);
    for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) jack_.addItem(juce::String(def.preamp.jacks[j].label), int(j) + 1);
    jackCaption_.setText(juce::String(def.preamp.jackLabel), juce::dontSendNotification);
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

void SasEditor::timerCallback() {
    // Follows the amp parameter when the host or a restored state changes it.
    if (processor_.selectedAmpIndex() != shownAmp_) showAmp(processor_.selectedAmpIndex());
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    if (jackNow < jack_.getNumItems() && jack_.getSelectedItemIndex() != jackNow)
        jack_.setSelectedItemIndex(jackNow, juce::dontSendNotification);

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
    g.fillAll(kBackground);

    for (const auto& p : panels_) {
        const auto b = p.bounds.toFloat();
        g.setColour(AmpLookAndFeel::panel());
        g.fillRoundedRectangle(b, 6.0f);
        g.setColour(AmpLookAndFeel::panelEdge());
        g.drawRoundedRectangle(b.reduced(0.5f), 6.0f, 1.0f);
        if (p.caption.isNotEmpty()) {
            g.setColour(juce::Colour::fromRGB(150, 128, 98));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(p.caption, p.bounds.getX() + 10, p.bounds.getY() + 4, p.bounds.getWidth() - 20, 16, juce::Justification::centredLeft);
        }
    }

    // Title of the amp, with the product name small above it.
    const int left = 12 + 176;
    g.setColour(juce::Colour::fromRGB(150, 128, 98));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("SCHEMATIC AMP SIM", left, 8, 300, 14, juce::Justification::centredLeft);
    g.setColour(AmpLookAndFeel::accent());
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText(title_, left, 20, getWidth() - left - 12, 28, juce::Justification::centredLeft);
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
    for (auto* c : std::initializer_list<juce::Component*>{&cabCaption_, &cab_, &loadIr_, &clearIr_, &irName_, &input_, &output_}) c->setVisible(ampPage);
    for (auto* c : std::initializer_list<juce::Component*>{&tsOn_, &tsDrive_, &tsTone_, &tsLevel_}) c->setVisible(!ampPage);

    if (!ampPage) {
        // Effects page: one panel per effect, side by side; only the overdrive for now.
        const int knobW = std::clamp(area.getWidth() / 8, 70, 110);
        auto tsPanel = area.removeFromTop(std::min(area.getHeight(), 190)).removeFromLeft(knobW * 3 + 12);
        panels_.push_back({tsPanel, "OVERDRIVE"});
        auto in = tsPanel.reduced(6, 4);
        auto head = in.removeFromTop(20);
        tsOn_.setBounds(head.removeFromRight(116));
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

    const int knobW = std::clamp(bottom.getWidth() / 6, 70, 110);
    auto ioPanel = bottom.removeFromRight(knobW * 2 + 12);
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
        input_.setBounds(in.removeFromLeft(in.getWidth() / 2));
        output_.setBounds(in);
    }
    repaint();
}
