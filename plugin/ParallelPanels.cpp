#include "ParallelPanels.h"

#include "RackState.h"

namespace {

juce::String pid(const char* base, int instance) { return RackState::paramId(base, instance); }

void caption(juce::Component& parent, juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
    parent.addAndMakeVisible(l);
}

void fillCombo(juce::Component& parent, juce::ComboBox& box, SasProcessor& p, const char* base, int instance,
               std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att) {
    parent.addAndMakeVisible(box);
    const auto id = pid(base, instance);
    const auto* param = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(id));
    box.addItemList(param->choices, 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, id, box);
}

// A choice whose items depend on the amp: the position goes to the parameter by hand.
void wireIndex(juce::ComboBox& box, SasProcessor& p, const juce::String& id) {
    box.onChange = [&box, &p, id] {
        if (auto* param = p.apvts.getParameter(id)) {
            const int index = box.getSelectedItemIndex();
            if (index < 0) return;
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(float(index)));
            param->endChangeGesture();
        }
    };
}

}  // namespace

// ------------------------------------------------------------------ amp B

AmpPanelB::AmpPanelB(SasProcessor& p)
    : processor_(p), input_(p.apvts, pid(ids::input, kInstance).toRawUTF8(), "INPUT dB"), cable_(p.apvts, pid(ids::cable, kInstance).toRawUTF8(), "CABLE m"),
      bias_(p.apvts, pid(ids::bias, kInstance).toRawUTF8(), "BIAS"), wear_(p.apvts, pid(ids::wear, kInstance).toRawUTF8(), "WEAR") {
    caption(*this, ampCaption_, "AMP B");
    fillCombo(*this, ampBox_, p, ids::amp, kInstance, ampAtt_);
    ampBox_.setComponentID("amp_b_select");
    caption(*this, qualityCaption_, "QUALITY");
    fillCombo(*this, quality_, p, ids::quality, kInstance, qualityAtt_);
    caption(*this, jackCaption_, "INPUT JACK");
    addAndMakeVisible(jack_);
    wireIndex(jack_, p, pid(ids::jack, kInstance));
    caption(*this, pickupCaption_, "PICKUP");
    fillCombo(*this, pickup_, p, ids::pickup, kInstance, pickupAtt_);
    caption(*this, tubeCaption_, "POWER TUBES");
    addAndMakeVisible(tube_);
    tube_.onChange = [this] {
        if (auto* param = processor_.apvts.getParameter(pid(ids::tube, kInstance))) {
            const int index = tube_.getSelectedItemIndex();
            if (index < 0 || shownAmp_ < 0) return;
            // The amp's own tube is "Stock"; the others are stored by type (see SasProcessor::wantedCondition).
            const auto& tubes = processor_.amps()[std::size_t(shownAmp_)].power.tubes;
            int type = 0;
            if (index > 0 && index < int(tubes.size()))
                for (int t = 1; t < 4; ++t)
                    if (tubes[std::size_t(index)].name == ids::tubeTypes[t]) type = t;
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(float(type)));
            param->endChangeGesture();
        }
    };
    caption(*this, noiseCaption_, "NOISE AND HUM");
    fillCombo(*this, noise_, p, ids::noise, kInstance, noiseAtt_);
    caption(*this, unitCaption_, "UNIT");
    fillCombo(*this, unit_, p, ids::unit, kInstance, unitAtt_);
    for (int i = 0; i < 4; ++i) {
        addChildComponent(selBox_[i]);
        addChildComponent(selCaption_[i]);
        caption(*this, selCaption_[i], "");
        selCaption_[i].setVisible(false);
        wireIndex(selBox_[i], p, pid(ids::sel[i], kInstance));
    }
    for (auto* k : {&input_, &cable_, &bias_, &wear_}) addAndMakeVisible(*k);
    input_.setComponentID("amp_b_input");
    note_.setText("The second amp has its own controls, circuits and condition. A NAM Loader amp here plays the model loaded in the first one.", juce::dontSendNotification);
    note_.setFont(AmpLookAndFeel::font(10.5f));
    addAndMakeVisible(note_);
    rebuildForAmp();
    startTimerHz(4);
}

void AmpPanelB::rebuildForAmp() {
    shownAmp_ = processor_.selectedAmpIndex(kInstance);
    const auto& def = processor_.amps()[std::size_t(shownAmp_)];
    knobs_.clear();
    for (const auto& u : def.ui) {
        auto k = std::make_unique<KnobWithLabel>(processor_.apvts, pid(u.param.c_str(), kInstance).toRawUTF8(), u.label);
        addAndMakeVisible(*k);
        knobs_.push_back(std::move(k));
    }
    jack_.clear(juce::dontSendNotification);
    for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) jack_.addItem(juce::String(def.preamp.jacks[j].label), int(j) + 1);
    jackCaption_.setText(juce::String(def.preamp.jackLabel), juce::dontSendNotification);
    jack_.setVisible(!def.preamp.jacks.empty());
    jackCaption_.setVisible(!def.preamp.jacks.empty());
    for (int i = 0; i < 4; ++i) {
        selBox_[i].clear(juce::dontSendNotification);
        const bool has = i < int(def.preamp.selectors.size());
        selBox_[i].setVisible(has);
        selCaption_[i].setVisible(has);
        if (!has) continue;
        const auto& sd = def.preamp.selectors[std::size_t(i)];
        for (std::size_t q = 0; q < sd.positions.size(); ++q) selBox_[i].addItem(juce::String(sd.positions[q].label), int(q) + 1);
        selCaption_[i].setText(juce::String(sd.label), juce::dontSendNotification);
    }
    tube_.clear(juce::dontSendNotification);
    for (std::size_t t = 0; t < def.power.tubes.size(); ++t) tube_.addItem(juce::String(def.power.tubes[t].name), int(t) + 1);
    tube_.setEnabled(def.power.tubes.size() > 1);
    bias_.setEnabled(def.power.bias.adjustable());
    bias_.setAlpha(def.power.bias.adjustable() ? 1.0f : 0.4f);
    resized();
}

void AmpPanelB::timerCallback() {
    if (processor_.selectedAmpIndex(kInstance) != shownAmp_) rebuildForAmp();
    if (shownAmp_ < 0) return;
    const auto& def = processor_.amps()[std::size_t(shownAmp_)];
    auto sync = [](juce::ComboBox& box, int now) {
        if (now < box.getNumItems() && box.getSelectedItemIndex() != now) box.setSelectedItemIndex(now, juce::dontSendNotification);
    };
    sync(jack_, int(processor_.apvts.getRawParameterValue(pid(ids::jack, kInstance))->load() + 0.5f));
    for (int i = 0; i < int(def.preamp.selectors.size()) && i < 4; ++i) sync(selBox_[i], int(processor_.apvts.getRawParameterValue(pid(ids::sel[i], kInstance))->load() + 0.5f));
    sync(tube_, processor_.wantedCondition(kInstance).tube);
}

void AmpPanelB::resized() {
    panels_.clear();
    auto area = getLocalBounds();
    constexpr int gap = 8;
    auto top = area.removeFromTop(190);
    area.removeFromTop(gap);
    auto bottom = area;
    // Top: the amp, its quality and jack, its own switches and the knobs of its front panel.
    panels_.push_back({top, "AMP B"});
    {
        auto in = top.reduced(8, 4);
        in.removeFromTop(18);
        auto left = in.removeFromLeft(200);
        ampCaption_.setBounds(left.removeFromTop(16));
        ampBox_.setBounds(left.removeFromTop(24));
        left.removeFromTop(4);
        qualityCaption_.setBounds(left.removeFromTop(16));
        quality_.setBounds(left.removeFromTop(24));
        left.removeFromTop(4);
        jackCaption_.setBounds(left.removeFromTop(16));
        jack_.setBounds(left.removeFromTop(24));
        in.removeFromLeft(10);
        int selectors = 0;
        for (int i = 0; i < 4; ++i)
            if (selBox_[i].isVisible()) ++selectors;
        if (selectors > 0) {
            auto strip = in.removeFromBottom(44);
            const int w = strip.getWidth() / selectors;
            for (int i = 0; i < 4; ++i)
                if (selBox_[i].isVisible()) {
                    auto cell = strip.removeFromLeft(w).reduced(6, 0);
                    selCaption_[i].setBounds(cell.removeFromTop(16));
                    selBox_[i].setBounds(cell.removeFromTop(24));
                }
        }
        const int n = std::max<int>(1, int(knobs_.size()));
        const int w = in.getWidth() / n;
        for (auto& k : knobs_) k->setBounds(in.removeFromLeft(w));
    }
    // Bottom: the guitar, the state of the amp and the speaker.
    auto guitar = bottom.removeFromLeft(210);
    bottom.removeFromLeft(gap);
    panels_.push_back({guitar, "GUITAR INPUT"});
    {
        auto in = guitar.reduced(8, 4);
        in.removeFromTop(18);
        auto knobs = in.removeFromRight(76);
        input_.setBounds(knobs.removeFromTop(knobs.getHeight() / 2));
        cable_.setBounds(knobs);
        pickupCaption_.setBounds(in.removeFromTop(16));
        pickup_.setBounds(in.removeFromTop(24));
    }
    panels_.push_back({bottom, "AMP CONDITION"});
    {
        auto in = bottom.reduced(8, 4);
        in.removeFromTop(18);
        note_.setBounds(in.removeFromBottom(30));
        auto col1 = in.removeFromLeft(130);
        tubeCaption_.setBounds(col1.removeFromTop(16));
        tube_.setBounds(col1.removeFromTop(24));
        bias_.setBounds(in.removeFromLeft(75));
        auto col2 = in.removeFromLeft(120);
        col2.removeFromLeft(10);
        noiseCaption_.setBounds(col2.removeFromTop(16));
        noise_.setBounds(col2.removeFromTop(24));
        wear_.setBounds(in.removeFromLeft(75));
        auto col3 = in.removeFromLeft(110);
        unitCaption_.setBounds(col3.removeFromTop(16));
        unit_.setBounds(col3.removeFromTop(24));
    }
    repaint();
}

// ------------------------------------------------------------------ cabinet B

CabinetPanelB::CabinetPanelB(SasProcessor& p)
    : processor_(p), blend_(p.apvts, pid(ids::irBlend, kInstance).toRawUTF8(), "BLEND"), width_(p.apvts, pid(ids::irWidth, kInstance).toRawUTF8(), "WIDTH"),
      doubleAmount_(p.apvts, pid(ids::doubleAmount, kInstance).toRawUTF8(), "DOUBLE"), doubleTime_(p.apvts, pid(ids::doubleTime, kInstance).toRawUTF8(), "TIME"),
      speaker_(p.apvts, pid(ids::speaker, kInstance).toRawUTF8(), "DYNAMICS") {
    caption(*this, cabCaption_, "SOURCE");
    fillCombo(*this, cab_, p, ids::cab, kInstance, cabAtt_);
    cab_.setComponentID("cabinet_b_source");
    for (auto* l : {&nameA_, &nameB_}) {
        l->setFont(AmpLookAndFeel::font(11.5f));
        addAndMakeVisible(*l);
    }
    for (auto* b : {&loadA_, &clearA_, &loadB_, &clearB_}) addAndMakeVisible(*b);
    addAndMakeVisible(curveA_);
    addAndMakeVisible(curveB_);
    for (auto* k : {&blend_, &width_, &doubleAmount_, &doubleTime_, &speaker_}) addAndMakeVisible(*k);
    speaker_.setComponentID("cabinet_b_speaker");
    addAndMakeVisible(intense_);
    intense_.setTooltip("Stronger cone, heating and break-up effects");
    intenseAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, pid(ids::speakerIntense, kInstance), intense_);
    loadA_.onClick = [this] { showImpulseMenu(0, loadA_); };
    loadB_.onClick = [this] { showImpulseMenu(1, loadB_); };
    clearA_.onClick = [this] { processor_.clearImpulseResponse(0, kInstance); };
    clearB_.onClick = [this] { processor_.clearImpulseResponse(1, kInstance); };
    note_.setText("IR plays the impulses above, mixed by the blend. Filter is a simple cabinet curve. The second cabinet has no NAM mode: it plays the filter.", juce::dontSendNotification);
    note_.setFont(AmpLookAndFeel::font(10.5f));
    addAndMakeVisible(note_);
    timerCallback();
    startTimerHz(4);
}

void CabinetPanelB::timerCallback() {
    for (int slot = 0; slot < 2; ++slot) {
        const auto name = processor_.impulseResponseName(slot, kInstance);
        (slot == 0 ? nameA_ : nameB_).setText(name.isEmpty() ? juce::String("empty") : name, juce::dontSendNotification);
        (slot == 0 ? curveA_ : curveB_).setCurve(processor_.impulseResponseCurve(slot, kInstance));
    }
}

void CabinetPanelB::showImpulseMenu(int slot, juce::Component& anchor) {
    juce::PopupMenu menu;
    for (int i = 0; i < SasProcessor::builtInImpulseResponseCount(); ++i) menu.addItem(100 + i, SasProcessor::builtInImpulseResponseName(i));
    menu.addSeparator();
    menu.addItem(1, "From file...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [this, slot](int choice) {
        if (choice == 1) {
            chooser_ = std::make_unique<juce::FileChooser>("Choose an impulse response", juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot](const juce::FileChooser& fc) {
                const auto file = fc.getResult();
                if (file == juce::File{}) return;
                if (processor_.loadImpulseResponse(file, slot, kInstance)) {
                    if (auto* cab = processor_.apvts.getParameter(pid(ids::cab, kInstance))) cab->setValueNotifyingHost(cab->convertTo0to1(2.0f));  // audible right away
                } else {
                    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim", "Could not read that impulse response.");
                }
            });
        } else if (choice >= 100 && processor_.loadBuiltInImpulseResponse(choice - 100, slot, kInstance)) {
            if (auto* cab = processor_.apvts.getParameter(pid(ids::cab, kInstance))) cab->setValueNotifyingHost(cab->convertTo0to1(2.0f));
        }
    });
}

void CabinetPanelB::resized() {
    panels_.clear();
    auto area = getLocalBounds();
    constexpr int gap = 8;
    auto top = area.removeFromTop(160);
    area.removeFromTop(gap);
    auto slotA = top.removeFromLeft((top.getWidth() - gap) / 2);
    top.removeFromLeft(gap);
    auto slotB = top;
    auto slot = [&](juce::Rectangle<int> panel, const char* title, juce::TextButton& load, juce::TextButton& clear, juce::Label& name, IrCurve& curve) {
        panels_.push_back({panel, title});
        auto in = panel.reduced(10, 6);
        in.removeFromTop(18);
        auto row = in.removeFromTop(24);
        clear.setBounds(row.removeFromRight(54));
        row.removeFromRight(6);
        load.setBounds(row.removeFromRight(70));
        row.removeFromRight(6);
        name.setBounds(row);
        in.removeFromTop(6);
        curve.setBounds(in);
    };
    slot(slotA, "IMPULSE A (B)", loadA_, clearA_, nameA_, curveA_);
    slot(slotB, "IMPULSE B (B)", loadB_, clearB_, nameB_, curveB_);
    auto cabPanel = area.removeFromLeft(200);
    area.removeFromLeft(gap);
    auto blendPanel = area.removeFromLeft(96);
    area.removeFromLeft(gap);
    auto speakerPanel = area.removeFromLeft(118);
    area.removeFromLeft(gap);
    auto stereoPanel = area;
    panels_.push_back({cabPanel, "CABINET B"});
    {
        auto in = cabPanel.reduced(10, 6);
        in.removeFromTop(20);
        cabCaption_.setBounds(in.removeFromTop(16));
        cab_.setBounds(in.removeFromTop(24));
        in.removeFromTop(6);
        note_.setBounds(in);
    }
    panels_.push_back({blendPanel, "A / B BLEND"});
    blend_.setBounds(blendPanel.reduced(6, 4).withTrimmedTop(16));
    panels_.push_back({speakerPanel, "SPEAKER"});
    {
        auto in = speakerPanel.reduced(6, 4);
        in.removeFromTop(16);
        intense_.setBounds(in.removeFromBottom(20).withSizeKeepingCentre(80, 20));
        speaker_.setBounds(in);
    }
    panels_.push_back({stereoPanel, "STEREO"});
    {
        auto in = stereoPanel.reduced(6, 4);
        in.removeFromTop(16);
        const int w = in.getWidth() / 3;
        width_.setBounds(in.removeFromLeft(w));
        doubleAmount_.setBounds(in.removeFromLeft(w));
        doubleTime_.setBounds(in);
    }
    repaint();
}
