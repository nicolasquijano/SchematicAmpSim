#include "I18n.h"
#include "PluginEditor.h"
#include "CircuitPage.h"
#include "UiSettings.h"
#include "IconData.h"
#include "TubeData.h"
#include "MultiPanel.h"
#include "ParallelPanels.h"
#include "RackPage.h"
#include "RackState.h"
#include "EqPresets.h"
#include "FxPresets.h"

#include <cmath>

#include "sas/version.h"

// ------------------------------------------------------------------ editor

SasEditor::SasEditor(SasProcessor& p)
    : AudioProcessorEditor(&p), processor_(p),
      input_(p.apvts, ids::input, "INPUT dB"), output_(p.apvts, ids::output, "OUTPUT dB"), cable_(p.apvts, ids::cable, "CABLE m"),
      bias_(p.apvts, ids::bias, "BIAS"), sag_(p.apvts, ids::sag, "SAG"), wear_(p.apvts, ids::wear, "WEAR"), speaker_(p.apvts, ids::speaker, "DYNAMICS"), blend_(p.apvts, ids::irBlend, "BLEND"),
      tunerRef_(p.apvts, ids::tunerRef, "REF Hz"), tunerSens_(p.apvts, ids::tunerSens, "SENS dB"), stereoWidth_(p.apvts, ids::irWidth, "WIDTH"), doubleAmount_(p.apvts, ids::doubleAmount, "DOUBLE"),
      doubleTime_(p.apvts, ids::doubleTime, "TIME") {
    setLookAndFeel(&look_);
    ampPanel_.onResized = [this] { layoutAmpPanel(); };
    cabPanel_.onResized = [this] { layoutCabPanel(); };

    if (const auto* amp = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(ids::amp))) ampNames_ = amp->choices;
    ampList_.setModel(this);
    ampList_.setRowHeight(26);
    addAndMakeVisible(ampList_);
    ampSearch_.setTextToShowWhenEmpty(tr("Search amps"), look_.text().withAlpha(0.45f));
    ampSearch_.setFont(AmpLookAndFeel::font(13.0f, false, 0.01f));
    ampSearch_.setMultiLine(false);
    ampSearch_.setReturnKeyStartsNewLine(false);
    ampSearch_.setEscapeAndReturnKeysConsumed(true);
    ampSearch_.setIndents(8, 0);
    ampSearch_.onTextChange = [this] { refreshAmpList(); };
    ampSearch_.onEscapeKey = [this] { ampSearch_.clear(); refreshAmpList(); };
    addAndMakeVisible(ampSearch_);
    refreshAmpList();

    styleCombo(quality_, ids::quality, qualityAtt_);
    styleCombo(cab_, ids::cab, cabAtt_);
    styleCombo(pickup_, ids::pickup, pickupAtt_);
    styleCaption(pickupCaption_, tr("PICKUP"));
    styleCombo(noise_, ids::noise, noiseAtt_);
    styleCombo(unit_, ids::unit, unitAtt_);
    styleCombo(tunerTuning_, ids::tunerTuning, tunerTuningAtt_);
    styleCaption(tunerTuningCaption_, tr("TUNING"));
    tunerTuning_.onChange = [this] { tunerDisplay_.setTuning(tunerTuning_.getSelectedItemIndex()); };
    styleCaption(noiseCaption_, tr("NOISE AND HUM"));
    styleCaption(unitCaption_, tr("UNIT"));
    styleCaption(tubeCaption_, tr("POWER TUBES"));
    addAndMakeVisible(tube_);
    tube_.onChange = [this] {
        if (auto* param = processor_.apvts.getParameter(ids::tube)) {
            const int index = tube_.getSelectedItemIndex();
            if (index < 0 || shownAmp_ < 0) return;
            // The amp's own tube is "Stock"; the others are stored by type (see wantedCondition).
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
    idleLabel_.setFont(AmpLookAndFeel::font(11.5f));
    addAndMakeVisible(idleLabel_);
    conditionNote_.setText(tr("Tubes, wear and unit rebuild the circuits; they act in the Normal, High and Efficient qualities."), juce::dontSendNotification);
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
    for (int i = 0; i < 6; ++i) {
        addChildComponent(selBox_[i]);
        addChildComponent(selCaption_[i]);
        styleCaption(selCaption_[i], "");
        selBox_[i].onChange = [this, i] {
            if (auto* param = processor_.apvts.getParameter(ids::sel[i])) {
                const int index = selBox_[i].getSelectedItemIndex();
                if (index < 0) return;
                param->beginChangeGesture();
                param->setValueNotifyingHost(param->convertTo0to1(float(index)));
                param->endChangeGesture();
                updateKnobActivity();
            }
        };
    }
    styleCaption(ampCaption_, tr("AMP"));
    styleCaption(qualityCaption_, tr("QUALITY"));
    styleCaption(jackCaption_, tr("INPUT JACK"));
    styleCaption(cabCaption_, tr("SOURCE"));
    addAndMakeVisible(tunerDisplay_);
    addAndMakeVisible(tunerOn_);
    tunerOnAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tunerOn, tunerOn_);
    addAndMakeVisible(tunerMute_);
    addAndMakeVisible(tunerSens_);
    addAndMakeVisible(tunerRef_);  // (it was never added: the reference pitch could not be changed from the editor)
    tunerMuteAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::tunerMute, tunerMute_);
    addAndMakeVisible(alignOn_);
    alignOn_.setTooltip(tr("Puts impulses A and B in phase: delays the earlier one and flips B if it is upside down, so the two do not cancel each other"));
    alignAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::irAlign, alignOn_);
    alignNote_.setFont(AmpLookAndFeel::font(9.5f));
    alignNote_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(alignNote_);
    multiPanel_ = std::make_unique<MultiPanel>(p);
    multiSpread_ = std::make_unique<KnobWithLabel>(p.apvts, ids::mcSpread, tr("SPREAD"));
    multiAlign_.setTooltip(tr("Lines all the impulses up with the first: flips the ones that are upside down and delays the early ones, so they do not cancel each other"));
    multiAlignAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::mcAlign, multiAlign_);
    multiAlignNote_.setFont(AmpLookAndFeel::font(9.5f));
    multiAlignNote_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(speakerIntense_);
    speakerIntense_.setTooltip(tr("Stronger cone, heating and break-up effects"));
    speakerIntenseAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::speakerIntense, speakerIntense_);
    addAndMakeVisible(vibrato_);
    vibratoAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, ids::vibOn, vibrato_);

    for (auto* l : {&nameA_, &nameB_}) {
        l->setFont(AmpLookAndFeel::font(11.5f));
        addAndMakeVisible(*l);
    }
    blendNote_.setFont(AmpLookAndFeel::font(10.5f));
    blendNote_.setJustificationType(juce::Justification::centred);
    blendNote_.setText("A  <  >  B", juce::dontSendNotification);
    addAndMakeVisible(blendNote_);
    cabNote_.setFont(AmpLookAndFeel::font(10.5f));
    for (juce::Button* b : std::initializer_list<juce::Button*>{&presets_, &alignOn_, &multiAlign_, &speakerIntense_, &tunerOn_, &tunerMute_, &vibrato_, &rackTab_, &tunerTab_, &circuitTab_, &loadA_, &clearA_, &loadB_, &clearB_,
                                                           &namLoad_, &namClear_, &namCabLoad_, &namCabClear_})
        b->setButtonText(tr(b->getButtonText()));  // the texts these were built with are English
    cabNote_.setText(tr("IR plays the impulses above, mixed by the blend. Filter is a simple cabinet curve; Off is the raw amp."), juce::dontSendNotification);
    addAndMakeVisible(cabNote_);
    for (auto* b : {&loadA_, &clearA_, &loadB_, &clearB_}) addAndMakeVisible(*b);
    addAndMakeVisible(curveA_);
    addAndMakeVisible(curveB_);
    loadA_.onClick = [this] { showImpulseMenu(0, loadA_); };
    loadB_.onClick = [this] { showImpulseMenu(1, loadB_); };
    clearA_.onClick = [this] { processor_.clearImpulseResponse(0); };
    clearB_.onClick = [this] { processor_.clearImpulseResponse(1); };

    for (auto* b : {&namLoad_, &namClear_}) addAndMakeVisible(*b);
    for (auto* l : {&namName_, &namNote_}) {
        l->setFont(AmpLookAndFeel::font(12.0f));
        addAndMakeVisible(*l);
    }
    namNote_.setFont(AmpLookAndFeel::font(10.5f));
    for (auto* b : {&namCabLoad_, &namCabClear_}) addAndMakeVisible(*b);
    namCabName_.setFont(AmpLookAndFeel::font(11.0f));
    addAndMakeVisible(namCabName_);
    namCabLoad_.onClick = [this] { chooseNamCab(); };
    namCabClear_.onClick = [this] {
        processor_.clearNamCab();
        refreshNamCab();
    };
    namLoad_.onClick = [this] { chooseNamModel(); };
    namClear_.onClick = [this] {
        processor_.clearNamModel();
        refreshNam();
    };
    addAndMakeVisible(presets_);
    presets_.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(headIn_);
    addAndMakeVisible(headOut_);
    for (auto* knob : {&headInKnob_, &headOutKnob_}) {
        addAndMakeVisible(*knob);
        knob->setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
        knob->setMouseDragSensitivity(160);
        knob->setPopupDisplayEnabled(true, true, this);  // the value, while it is dragged
    }
    headInAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids::input, headInKnob_);
    headOutAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids::output, headOutKnob_);
    headInKnob_.setDoubleClickReturnValue(true, p.apvts.getParameter(ids::input)->convertFrom0to1(p.apvts.getParameter(ids::input)->getDefaultValue()));
    headOutKnob_.setDoubleClickReturnValue(true, p.apvts.getParameter(ids::output)->convertFrom0to1(p.apvts.getParameter(ids::output)->getDefaultValue()));
    headInKnob_.setTooltip(tr("Input level (dB), in front of the first amp"));
    headOutKnob_.setTooltip(tr("Output level (dB)"));
    for (auto* b : {&headInAuto_, &headOutAuto_}) {
        addAndMakeVisible(*b);
        b->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa89a7a));
    }
    headInAuto_.setTooltip(tr("Play your loudest for 5 seconds: sets the input level so the peaks reach -10 dBFS"));
    headOutAuto_.setTooltip(tr("Play your loudest for 5 seconds: sets the output level so the peaks reach -6 dBFS"));
    headInAuto_.onClick = [this] { toggleLevelMeasure(1); };
    headOutAuto_.onClick = [this] { toggleLevelMeasure(2); };

    // Who lives where: the left column and the tuner stay with the editor; the amp's controls and the cabinet's go to their panels, which the
    // amp unit and the cabinet unit of the rack page host.
    for (auto* c : std::initializer_list<juce::Component*>{&input_, &output_, &cable_, &bias_, &sag_, &wear_, &inBar_, &outBar_, &tubes_, &pickup_, &pickupCaption_,
                                                           &tube_, &tubeCaption_, &noise_, &noiseCaption_, &unit_, &unitCaption_, &idleLabel_, &conditionNote_, &namLoad_, &namClear_,
                                                           &namName_, &namNote_, &selBox_[0], &selBox_[1], &selBox_[2], &selBox_[3], &selBox_[4], &selBox_[5], &selCaption_[0], &selCaption_[1], &selCaption_[2], &selCaption_[3], &selCaption_[4], &selCaption_[5]})
        ampPanel_.addChildComponent(*c);
    for (auto* c : std::initializer_list<juce::Component*>{&cab_, &cabCaption_, &blend_, &blendNote_, &alignOn_, &alignNote_, &loadA_, &clearA_, &loadB_, &clearB_, &nameA_, &nameB_, &curveA_, &curveB_, &cabNote_,
                                                           &stereoWidth_, &doubleAmount_, &doubleTime_, &namCabLoad_, &namCabClear_, &namCabName_, &speaker_, &speakerIntense_})
        cabPanel_.addAndMakeVisible(*c);
    for (auto* c : std::initializer_list<juce::Component*>{multiPanel_.get(), multiSpread_.get(), &multiAlign_, &multiAlignNote_}) cabPanel_.addChildComponent(*c);
    for (auto* c : std::initializer_list<juce::Component*>{&cable_, &bias_, &sag_, &wear_, &tubes_, &pickup_, &pickupCaption_,
                                                           &tube_, &tubeCaption_, &noise_, &noiseCaption_, &unit_, &unitCaption_, &idleLabel_, &conditionNote_})
        c->setVisible(true);
    for (auto* c : std::initializer_list<juce::Component*>{&input_, &output_, &inBar_, &outBar_}) c->setVisible(false);  // their place is the header now
    speaker_.setComponentID("speaker_dynamics");
    tube_.setComponentID("cond_tube");
    bias_.setComponentID("cond_bias");
    sag_.setComponentID("cond_sag");
    wear_.setComponentID("cond_wear");
    unit_.setComponentID("cond_unit");
    noise_.setComponentID("cond_noise");

    ampPanelB_ = std::make_unique<AmpPanelB>(p);
    cabPanelB_ = std::make_unique<CabinetPanelB>(p);
    rackPage_ = std::make_unique<RackPage>(p, [this](sas::ModuleType type, int instance) -> std::unique_ptr<ModuleUnit::Body> {
        if (type == sas::ModuleType::Amp && instance == 0) return std::make_unique<PanelBody>(ampPanel_, [this] { return ampPanelHeight(); });
        if (type == sas::ModuleType::Cabinet && instance == 0) return std::make_unique<PanelBody>(cabPanel_, [this] { return cabPanelHeight(); });
        if (type == sas::ModuleType::Amp) return std::make_unique<PanelBody>(*ampPanelB_, [this] { return ampPanelB_->preferredHeight(); });
        if (type == sas::ModuleType::Cabinet) return std::make_unique<PanelBody>(*cabPanelB_, [this] { return cabPanelB_->preferredHeight(); });
        return nullptr;
    });
    addAndMakeVisible(*rackPage_);
    circuitPage_ = std::make_unique<CircuitPage>(p, look_);
    addChildComponent(*circuitPage_);
    for (auto* t : {&rackTab_, &tunerTab_, &circuitTab_}) {
        t->setClickingTogglesState(true);
        t->setRadioGroupId(7);
        addAndMakeVisible(*t);
    }
    rackTab_.setToggleState(true, juce::dontSendNotification);
    rackTab_.onClick = [this] { setPage(0); };
    tunerTab_.onClick = [this] { setPage(1); };
    circuitTab_.onClick = [this] { setPage(2); };

    showAmp(processor_.selectedAmpIndex());
    setResizable(true, true);
    setResizeLimits(920, 480, 3840, 2400);
    {
        // The size the window had when it was last closed (kept between sessions and instances), if it still fits the screen.
        auto size = UiSettings::windowSize();
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea
                                                                                          : juce::Rectangle<int>(0, 0, 1920, 1080);
        if (size.x < 920 || size.y < 480) size = {1000, 560};
        setSize(std::min(size.x, std::max(920, area.getWidth())), std::min(size.y, std::max(480, area.getHeight())));
    }
    startTimerHz(30);
    timerCallback();
}

SasEditor::~SasEditor() {
    multiPanel_.reset();
    ampList_.setModel(nullptr);
    setLookAndFeel(nullptr);
}

// ------------------------------------------------------------------ amp list

int SasEditor::getNumRows() { return int(shown_.size()); }

namespace {
// The star at the right end of an amp's row: filled when the amp is a favourite, a click toggles it. Only the star takes the mouse; the rest of
// the row goes on to the list (which chooses the amp).
class StarCell final : public juce::Component {
public:
    std::function<void()> onToggle;
    bool favourite = false;
    bool hitTest(int x, int) override { return x >= getWidth() - 28; }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mouseWasClicked() && onToggle) onToggle();
    }
    void paint(juce::Graphics& g) override {
        const auto* look = dynamic_cast<const AmpLookAndFeel*>(&getLookAndFeel());
        const auto colour = look ? look->accent() : juce::Colours::orange;
        const auto c = juce::Point<float>(float(getWidth()) - 16.0f, float(getHeight()) * 0.5f);
        juce::Path star;
        star.addStar(c, 5, 3.2f, 7.0f, -juce::MathConstants<float>::halfPi);
        if (favourite) {
            g.setColour(colour);
            g.fillPath(star);
        } else {
            g.setColour((look ? look->label() : juce::Colours::grey).withAlpha(0.45f));
            g.strokePath(star, juce::PathStrokeType(1.0f));
        }
    }
};
}  // namespace

juce::Component* SasEditor::refreshComponentForRow(int row, bool, juce::Component* existing) {
    if (row < 0 || row >= int(shown_.size()) || shown_[std::size_t(row)] < 0) {  // a group header has no star
        delete existing;
        return nullptr;
    }
    auto* cell = dynamic_cast<StarCell*>(existing);
    if (!cell) {
        delete existing;
        cell = new StarCell;
    }
    const int amp = shown_[std::size_t(row)];
    const auto& defs = processor_.amps();
    const juce::String id = amp < int(defs.size()) ? juce::String(defs[std::size_t(amp)].id) : juce::String();
    cell->favourite = UiSettings::isFavourite(id);
    cell->onToggle = [this, id] {
        UiSettings::toggleFavourite(id);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SasEditor>(this)] {
            if (safe != nullptr) safe->refreshAmpList();
        });
    };
    cell->repaint();
    return cell;
}

void SasEditor::refreshAmpList() {
    const auto query = ampSearch_.getText().trim();
    shown_.clear();
    // The favourites and the amps used last go first (when nothing is being searched); they are also in their own groups below.
    if (query.isEmpty()) {
        const auto& all = processor_.amps();
        const auto indexOf = [&](const juce::String& id) {
            for (std::size_t i = 0; i < all.size(); ++i)
                if (juce::String(all[i].id) == id) return int(i);
            return -1;
        };
        std::vector<int> fav;
        for (const auto& id : UiSettings::favourites())
            if (const int i = indexOf(id); i >= 0 && i < ampNames_.size()) fav.push_back(i);
        std::sort(fav.begin(), fav.end(), [&](int a, int b) { return ampNames_[a].compareIgnoreCase(ampNames_[b]) < 0; });
        if (!fav.empty()) {
            shown_.push_back(-6);
            shown_.insert(shown_.end(), fav.begin(), fav.end());
        }
    }
    // Grouped by character, each group in alphabetical order; a group only shows when something in it matches.
    static const char* const kGroups[] = {"clean", "crunch", "highgain", "bass", "other"};
    const auto& defs = processor_.amps();
    for (int g = 0; g < 5; ++g) {
        std::vector<int> members;
        for (int i = 0; i < ampNames_.size(); ++i) {
            std::string cat = i < int(defs.size()) ? defs[std::size_t(i)].category : "other";
            bool known = false;
            for (const char* k : kGroups) known = known || cat == k;
            if (!known) cat = "other";
            if (cat == kGroups[g] && (query.isEmpty() || ampNames_[i].containsIgnoreCase(query))) members.push_back(i);
        }
        if (members.empty()) continue;
        std::sort(members.begin(), members.end(), [&](int a, int b) { return ampNames_[a].compareIgnoreCase(ampNames_[b]) < 0; });
        shown_.push_back(-(g + 1));
        shown_.insert(shown_.end(), members.begin(), members.end());
    }
    ampList_.updateContent();
    juce::SparseSet<int> sel;
    // An amp that is in a section at the top (favourites) is also in its group below; the row that was clicked keeps the highlight.
    const auto rit = std::find(shown_.rbegin(), shown_.rend(), processor_.selectedAmpIndex());
    const auto fit = std::find(shown_.begin(), shown_.end(), processor_.selectedAmpIndex());
    if (rit != shown_.rend()) {
        const int row = pickedFromTop_ ? int(fit - shown_.begin()) : int(shown_.rend() - rit) - 1;
        sel.addRange(juce::Range<int>(row, row + 1));
        ampList_.setSelectedRows(sel, juce::dontSendNotification);
        if (!pickedFromTop_) ampList_.scrollToEnsureRowIsOnscreen(row);
    } else {
        ampList_.setSelectedRows(sel, juce::dontSendNotification);
    }
    ampList_.repaint();
}

void SasEditor::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) {
    if (row < 0 || row >= int(shown_.size())) return;
    if (shown_[std::size_t(row)] < 0) {  // the header of a group
        static const char* const kTitles[] = {"Clean amps", "Crunch amps", "High-gain amps", "Bass amps", "Other", "Favorites"};
        g.setColour(look_.label());
        g.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
        g.drawText(tr(kTitles[-shown_[std::size_t(row)] - 1]).toUpperCase(), 10, 4, w - 14, h - 4, juce::Justification::centredLeft);
        g.setColour(look_.label().withAlpha(0.25f));
        g.fillRect(10, h - 1, w - 20, 1);
        return;
    }
    if (selected) {
        g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.26f), 0.0f, 0.0f, look_.accent().withAlpha(0.06f), float(w), 0.0f, false));
        g.fillRect(0, 0, w, h);
        g.setColour(look_.accent());
        g.fillRoundedRectangle(0.0f, 4.0f, 3.0f, float(h - 8), 1.5f);
    }
    g.setColour(selected ? look_.accent() : look_.text().withAlpha(0.8f));
    g.setFont(AmpLookAndFeel::font(14.0f, selected, 0.01f));
    g.drawText(ampNames_[shown_[std::size_t(row)]], 12, 0, w - 16, h, juce::Justification::centredLeft);
}

void SasEditor::selectedRowsChanged(int lastRow) {
    if (lastRow < 0 || lastRow >= int(shown_.size())) return;
    const int amp = shown_[std::size_t(lastRow)];
    if (amp < 0) {  // a header cannot be chosen: the list goes back to the amp in use
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SasEditor>(this)] {
            if (safe != nullptr) safe->refreshAmpList();
        });
        return;
    }
    {
        std::size_t firstGroup = 0;  // the first row of the ordinary groups: the rows before it are the favourites
        while (firstGroup < shown_.size() && !(shown_[firstGroup] <= -1 && shown_[firstGroup] >= -5)) ++firstGroup;
        pickedFromTop_ = std::size_t(lastRow) < firstGroup;
    }
    if (amp == processor_.selectedAmpIndex()) return;
    if (auto* param = processor_.apvts.getParameter(ids::amp)) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(float(amp)));
        param->endChangeGesture();
    }
    processor_.resetTubes(0);  // the new amp comes with its own tubes
    processor_.applyDefaultAmpPreset(0);
}

void SasEditor::styleCombo(juce::ComboBox& box, const char* paramId,
                           std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att) {
    addAndMakeVisible(box);
    // Items first, then the attachment selects the stored one.
    const auto* param = dynamic_cast<juce::AudioParameterChoice*>(processor_.apvts.getParameter(paramId));
    box.addItemList(trAll(param->choices), 1);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor_.apvts, paramId, box);
}

void SasEditor::styleCaption(juce::Label& l, const juce::String& text) {
    l.setText(tr(text), juce::dontSendNotification);
    l.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
    l.setColour(juce::Label::textColourId, look_.label());
    addAndMakeVisible(l);
}

void SasEditor::applyTheme(const sas::AmpDef& def) {
    look_.setTheme(EditorTheme::from(def.theme));
    for (auto* l : {&ampCaption_, &qualityCaption_, &jackCaption_, &cabCaption_, &pickupCaption_, &tubeCaption_, &noiseCaption_, &unitCaption_, &conditionNote_})
        l->setColour(juce::Label::textColourId, look_.label());
    for (auto* l : {&nameA_, &nameB_, &blendNote_, &cabNote_, &namName_, &namNote_, &namCabName_}) l->setColour(juce::Label::textColourId, look_.label());
    sendLookAndFeelChange();
    repaint();
}

void SasEditor::setPage(int page) {
    page_ = page;
    for (int i = 0; i < 3; ++i) (i == 0 ? rackTab_ : i == 1 ? tunerTab_ : circuitTab_).setToggleState(i == page, juce::dontSendNotification);
    resized();
}

void SasEditor::showAmp(int index) {
    const auto& defs = processor_.amps();
    if (index < 0 || index >= int(defs.size())) return;
    shownAmp_ = index;
    const auto& def = defs[std::size_t(index)];
    title_ = juce::String(def.name);
    circuitPage_->setAmp(def);
    applyTheme(def);

    ampKnobs_.clear();
    face_.reset();
    if (AmpFace::available(def)) {
        face_ = std::make_unique<AmpFace>(processor_.apvts, def);
        ampPanel_.addAndMakeVisible(*face_);
    }
    for (const auto& u : def.ui) {
        auto k = std::make_unique<KnobWithLabel>(processor_.apvts, u.param.c_str(), u.label, u.style == "slider");
        ampPanel_.addAndMakeVisible(*k);
        ampKnobs_.push_back(std::move(k));
    }

    jack_.clear(juce::dontSendNotification);
    for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) jack_.addItem(tr(def.preamp.jacks[j].label), int(j) + 1);
    jackCaption_.setText(tr(def.preamp.jackLabel), juce::dontSendNotification);
    for (int i = 0; i < 6; ++i) {
        selBox_[i].clear(juce::dontSendNotification);
        const bool has = i < int(def.preamp.selectors.size());
        selBox_[i].setVisible(has);
        selCaption_[i].setVisible(has);
        if (!has) continue;
        const auto& sd = def.preamp.selectors[std::size_t(i)];
        for (std::size_t p = 0; p < sd.positions.size(); ++p) selBox_[i].addItem(tr(sd.positions[p].label), int(p) + 1);
        selCaption_[i].setText(tr(sd.label), juce::dontSendNotification);
        const int now = int(processor_.apvts.getRawParameterValue(ids::sel[i])->load() + 0.5f);
        selBox_[i].setSelectedItemIndex(juce::jlimit(0, int(sd.positions.size()) - 1, now), juce::dontSendNotification);
    }
    tube_.clear(juce::dontSendNotification);
    for (std::size_t t = 0; t < def.power.tubes.size(); ++t) tube_.addItem(juce::String(def.power.tubes[t].name), int(t) + 1);
    tube_.setSelectedItemIndex(processor_.wantedCondition().tube, juce::dontSendNotification);
    tube_.setEnabled(def.power.tubes.size() > 1);
    bias_.setEnabled(def.power.bias.adjustable());
    bias_.setAlpha(def.power.bias.adjustable() ? 1.0f : 0.4f);
    vibrato_.setVisible(def.preamp.vibrato.present);
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    if (!def.preamp.jacks.empty()) jack_.setSelectedItemIndex(juce::jlimit(0, int(def.preamp.jacks.size()) - 1, jackNow), juce::dontSendNotification);
    refreshAmpList();
    refreshNam();
    refreshNamCab();
    ampPanel_.resized();
    updateKnobActivity();
    if (rackPage_) rackPage_->relayout();
    if (getWidth() > 0) resized();  // the new name may need another split of the header
    repaint();
}

void SasEditor::updateKnobActivity() {
    if (shownAmp_ < 0 || shownAmp_ >= int(processor_.amps().size())) return;
    const auto& def = processor_.amps()[std::size_t(shownAmp_)];
    for (std::size_t i = 0; i < ampKnobs_.size() && i < def.ui.size(); ++i) {
        bool active = true;
        for (const auto& [name, positions] : def.ui[i].activeWhen)
            for (std::size_t s = 0; s < def.preamp.selectors.size() && s < 6; ++s) {
                if (def.preamp.selectors[s].name != name) continue;
                const int now = int(processor_.apvts.getRawParameterValue(ids::sel[s])->load() + 0.5f);
                active = active && std::find(positions.begin(), positions.end(), now) != positions.end();
            }
        const float alpha = active ? 1.0f : 0.35f;
        if (ampKnobs_[i]->getAlpha() != alpha) ampKnobs_[i]->setAlpha(alpha);
    }
}

void SasEditor::pollMeters() {
    const float in = processor_.takeInputPeak(), out = processor_.takeOutputPeak();
    inBar_.setLevel(in);
    outBar_.setLevel(out);
    headIn_.setLevel(in);
    headOut_.setLevel(out);
    repaint(cpuBounds_);
    if (shownAmp_ >= 0 && shownAmp_ < int(processor_.amps().size())) {
        const auto& def = processor_.amps()[std::size_t(shownAmp_)];
        const int idx = def.power.tubes.empty() ? 0 : std::min(processor_.wantedCondition().tube, int(def.power.tubes.size()) - 1);
        const double pmax = def.power.tubes.empty() ? 25.0 : def.power.tubes[std::size_t(idx)].pmaxW;
        const juce::String name = def.power.tubes.empty() ? juce::String("tubes") : juce::String(def.power.tubes[std::size_t(idx)].name);
        if (def.power.tubes.empty()) {
            tubes_.setTransistors(def.power.outputTransistors, tr("Output transistors"));  // a transistor output stage: no tubes to show
        } else {
            tubes_.setTubes(int(def.power.bias.tubesPerHalf * 2.0 + 0.5), name);
            const float ma = processor_.idleMilliamps();
            tubes_.setGlow(float(ma / (pmax / 0.42)), ma);  // dissipation ~ current x about 420 V of plate voltage
        }
    }
}

void SasEditor::toggleLevelMeasure(int side) {
    if (levelSide_ == side) {  // pressed again: cancelled
        levelSide_ = 0;
    } else {
        levelSide_ = side;
        levelStartMs_ = juce::Time::getMillisecondCounter();
        processor_.resetLevelMax();
    }
    levelNoteSide_ = 0;
    updateLevelButtons();
}

void SasEditor::finishLevelMeasure() {
    const int side = levelSide_;
    levelSide_ = 0;
    auto* param = processor_.apvts.getParameter(side == 1 ? ids::input : ids::output);
    const float peak = side == 1 ? processor_.inputMax() : processor_.outputMax();
    if (peak < 1e-4f) {  // nothing was played: say so for a moment and leave the knob alone
        levelNoteSide_ = side;
        levelNoteUntilMs_ = juce::Time::getMillisecondCounter() + 2500;
    } else {
        const float now = param->convertFrom0to1(param->getValue());
        const float db = side == 1 ? SasProcessor::autoInputDb(peak) : SasProcessor::autoOutputDb(now, peak);
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(db));
        param->endChangeGesture();
    }
    updateLevelButtons();
}

void SasEditor::updateLevelButtons() {
    const auto now = juce::Time::getMillisecondCounter();
    if (levelNoteSide_ != 0 && now > levelNoteUntilMs_) levelNoteSide_ = 0;
    auto text = [&](int side) {
        if (levelSide_ == side) return juce::String(juce::jmax(1, int(std::ceil((5000.0 - double(now - levelStartMs_)) / 1000.0)))) + " s";
        if (levelNoteSide_ == side) return tr("No signal");
        return tr("Auto");
    };
    headInAuto_.setButtonText(text(1));
    headOutAuto_.setButtonText(text(2));
    headInAuto_.setToggleState(levelSide_ == 1, juce::dontSendNotification);
    headOutAuto_.setToggleState(levelSide_ == 2, juce::dontSendNotification);
}

void SasEditor::timerCallback() {
    if (sizeDirty_ && juce::Time::getMillisecondCounter() - sizeChangedMs_ > 600) {
        sizeDirty_ = false;
        UiSettings::setWindowSize(getWidth(), getHeight());
    }
    pollMeters();
    if (levelSide_ != 0 && juce::Time::getMillisecondCounter() - levelStartMs_ >= 5000) finishLevelMeasure();
    if (tick_ % 4 == 0) updateLevelButtons();
    if (++tunerTicks_ % 2 == 0) updateTuner();
    if (page_ == 2) circuitPage_->tick();
    if (tick_++ % 8 != 0) return;  // the rest at about 4 Hz
    // Follows the amp parameter when the host or a restored state changes it.
    if (processor_.selectedAmpIndex() != shownAmp_) showAmp(processor_.selectedAmpIndex());
    updateCabMode();
    if (face_) face_->repaint();  // the jacks follow the parameter
    refreshNam();
    refreshNamCab();
    const int jackNow = int(processor_.apvts.getRawParameterValue(ids::jack)->load() + 0.5f);
    if (jackNow < jack_.getNumItems() && jack_.getSelectedItemIndex() != jackNow)
        jack_.setSelectedItemIndex(jackNow, juce::dontSendNotification);
    for (int i = 0; i < 6; ++i) {
        const int now = int(processor_.apvts.getRawParameterValue(ids::sel[i])->load() + 0.5f);
        if (now < selBox_[i].getNumItems() && selBox_[i].getSelectedItemIndex() != now) selBox_[i].setSelectedItemIndex(now, juce::dontSendNotification);
    }
    updateKnobActivity();

    if (const int tubeNow = processor_.wantedCondition().tube;
        tubeNow < tube_.getNumItems() && tube_.getSelectedItemIndex() != tubeNow)
        tube_.setSelectedItemIndex(tubeNow, juce::dontSendNotification);
    {
        const float ma = processor_.idleMilliamps();
        idleLabel_.setText(ma > 0.5f ? juce::String(ma, 1) + tr(" mA per tube") : juce::String(tr("plate current: Normal / High only")), juce::dontSendNotification);
    }

    updateImpulseStatus();
}

void SasEditor::updateTuner() {
    const bool active = processor_.apvts.getRawParameterValue(ids::tunerOn)->load() > 0.5f;
    const double ref = double(processor_.apvts.getRawParameterValue(ids::tunerRef)->load());
    const int tuning = juce::jlimit(0, sas::kTuningCount - 1, int(processor_.apvts.getRawParameterValue(ids::tunerTuning)->load() + 0.5f));
    const double sensDb = double(processor_.apvts.getRawParameterValue(ids::tunerSens)->load());
    tunerDisplay_.setTuning(tuning);
    bool valid = false, held = false;
    double levelDb = -120.0;
    if (active) {
        const double rate = processor_.tunerFeed().rate();
        const double minHz = sas::lowestHzFor(sas::kTunings[tuning], ref);
        // A short window for a tuning without low strings, so the reading follows the string quickly.
        const std::size_t n = std::min(tunerBuf_.size(), sas::PitchDetector::analysisWindow(rate, minHz));
        if (processor_.tunerFeed().copyLatest(tunerBuf_.data(), n)) {
            const std::size_t m = std::min<std::size_t>(1024, n);
            double e = 0.0;
            for (std::size_t i = n - m; i < n; ++i) e += double(tunerBuf_[i]) * tunerBuf_[i];
            levelDb = 10.0 * std::log10(e / double(m) + 1e-12);
            const auto r = sas::PitchDetector::detect(tunerBuf_.data(), n, rate, minHz, 1400.0, std::pow(10.0, sensDb / 20.0));
            if (r.valid) {
                // Take a jump of more than a semitone only after it repeats, so one bad reading does not flash a wrong note.
                const bool jump = tunerHz_ > 0.0 && std::abs(12.0 * std::log2(r.hz / tunerHz_)) > 1.0;
                if (!jump || ++tunerHold_ >= 2) {
                    tunerHz_ = r.hz;
                    tunerHold_ = 0;
                }
                valid = true;
                tunerHeld_ = 0;
            } else {
                tunerHold_ = 0;
            }
        }
        // The string fades or the pick is between notes: the last reading stays on the screen for about 0.6 s, dimmed.
        if (!valid && tunerHz_ > 0.0) {
            if (++tunerHeld_ <= 9) held = true;
            else tunerHz_ = 0.0;
        }
    } else {
        tunerHz_ = 0.0;
        tunerHeld_ = 0;
    }
    if (page_ == 1 || active) {
        tunerDisplay_.setReading((valid || held) && tunerHz_ > 0.0, active, tunerHz_, ref, held);
        tunerDisplay_.setLevel(float(levelDb), float(sensDb));
    }
}

void SasEditor::showModule(sas::SlotRef slot) { rackPage_->select(slot); }

void SasEditor::updateImpulseStatus() {
    for (int slot = 0; slot < 2; ++slot) {
        const auto name = processor_.impulseResponseName(slot);
        (slot == 0 ? nameA_ : nameB_).setText(name.isEmpty() ? juce::String(tr("empty")) : name, juce::dontSendNotification);
        (slot == 0 ? curveA_ : curveB_).setCurve(processor_.impulseResponseCurve(slot));
    }
    alignNote_.setText(irAlignText(processor_.irAlignInfo(0)), juce::dontSendNotification);
}

// The Presets button: save the current state, load a file, or pick one of the presets saved in the presets folder.
void SasEditor::showPresetMenu() {
    const auto folder = SasProcessor::presetFolder();
    juce::PopupMenu menu;
    menu.addItem(1, tr("Save preset..."));
    menu.addItem(2, tr("Load preset file..."));
    menu.addItem(3, tr("Open the presets folder"));
    {  // the language of the interface: it applies the next time the editor window is opened
        juce::PopupMenu languages;
        const auto chosen = i18n::preference();
        for (auto lang : {i18n::Language::Auto, i18n::Language::English, i18n::Language::Spanish, i18n::Language::Portuguese, i18n::Language::Italian, i18n::Language::Chinese, i18n::Language::French, i18n::Language::German, i18n::Language::Japanese, i18n::Language::Russian, i18n::Language::Korean})
            languages.addItem(10 + int(lang), i18n::languageName(lang), true, chosen == lang);
        menu.addSeparator();
        menu.addSubMenu(tr("Language"), languages);
    }
    juce::Array<juce::File> files;
    folder.findChildFiles(files, juce::File::findFiles, false, "*.sasp");
    files.sort();
    if (!files.isEmpty()) {
        menu.addSeparator();
        for (int i = 0; i < files.size() && i < 200; ++i) menu.addItem(100 + i, files[i].getFileNameWithoutExtension());
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presets_), [this, folder, files](int choice) {
        auto load = [this](const juce::File& f) {
            if (!processor_.loadPreset(f))
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim", tr("Could not read that preset."));
        };
        if (choice >= 10 && choice <= 12) {
            i18n::setPreference(static_cast<i18n::Language>(choice - 10));
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Schematic Amp Sim", tr("The language changes the next time the editor window is opened."));
        } else if (choice == 1) {
            folder.createDirectory();
            chooser_ = std::make_unique<juce::FileChooser>(tr("Save the preset"), folder.getChildFile("My preset.sasp"), "*.sasp");
            chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this](const juce::FileChooser& fc) {
                                      auto file = fc.getResult();
                                      if (file == juce::File{}) return;
                                      if (!file.hasFileExtension("sasp")) file = file.withFileExtension("sasp");
                                      if (!processor_.savePreset(file))
                                          juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim", tr("Could not write that file."));
                                  });
        } else if (choice == 2) {
            chooser_ = std::make_unique<juce::FileChooser>(tr("Load a preset"), folder, "*.sasp");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [load](const juce::FileChooser& fc) {
                const auto file = fc.getResult();
                if (file != juce::File{}) load(file);
            });
        } else if (choice == 3) {
            folder.createDirectory();
            folder.revealToUser();
        } else if (choice >= 100 && choice - 100 < files.size()) {
            load(files[choice - 100]);
        }
    });
}

void SasEditor::showImpulseMenu(int slot, juce::Component& anchor) {
    juce::PopupMenu menu;
    for (int i = 0; i < SasProcessor::builtInImpulseResponseCount(); ++i) menu.addItem(100 + i, SasProcessor::builtInImpulseResponseName(i));
    menu.addSeparator();
    menu.addItem(1, tr("From file..."));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [this, slot](int choice) {
        if (choice == 1) {
            chooseImpulseResponse(slot);
        } else if (choice >= 100 && processor_.loadBuiltInImpulseResponse(choice - 100, slot)) {
            if (auto* cab = processor_.apvts.getParameter(ids::cab)) cab->setValueNotifyingHost(cab->convertTo0to1(2.0f));  // audible right away
        }
    });
}

void SasEditor::chooseNamModel() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Choose a Neural Amp Modeler model"), juce::File{}, "*.nam");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File{}) return;
        if (!processor_.loadNamModel(file))
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim",
                                                   tr("Could not load that model: ") + i18n::namMessage(processor_.namModelMessage()) + ".");
        refreshNam();
    });
}

// The loader's model name and any message about it; cheap enough to run on the timer.
void SasEditor::refreshNam() {
    const bool loaded = processor_.namModelLoaded();
    const auto name = processor_.namModelName();
    namName_.setText(loaded ? name : juce::String(tr("No model loaded")), juce::dontSendNotification);
    namNote_.setText(loaded ? i18n::namMessage(processor_.namModelMessage()) : juce::String(tr("Load a .nam file; the signal passes through unchanged until then.")),
                     juce::dontSendNotification);
    namClear_.setEnabled(loaded);
}

void SasEditor::chooseNamCab() {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Choose a Neural Amp Modeler cabinet model"), juce::File{}, "*.nam");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File{}) return;
        if (processor_.loadNamCab(file)) {
            // Switch the cabinet to NAM so the load is audible right away.
            if (auto* cab = processor_.apvts.getParameter(ids::cab)) cab->setValueNotifyingHost(cab->convertTo0to1(3.0f));
        } else {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim",
                                                   tr("Could not load that model: ") + i18n::namMessage(processor_.namCabMessage()) + ".");
        }
        refreshNamCab();
    });
}

void SasEditor::refreshNamCab() {
    const bool loaded = processor_.namCabLoaded();
    const auto msg = i18n::namMessage(processor_.namCabMessage());
    namCabName_.setText(loaded ? processor_.namCabName() + (msg.isEmpty() ? juce::String() : "  (" + msg + ")") : juce::String(tr("no NAM cabinet")), juce::dontSendNotification);
    namCabClear_.setEnabled(loaded);
}

void SasEditor::chooseImpulseResponse(int slot) {
    chooser_ = std::make_unique<juce::FileChooser>(tr("Choose a cabinet impulse response"), juce::File{}, "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File{}) return;
                              if (processor_.loadImpulseResponse(file, slot)) {
                                  // Switch the cabinet to IR so the load is audible right away.
                                  if (auto* cab = processor_.apvts.getParameter(ids::cab)) cab->setValueNotifyingHost(cab->convertTo0to1(2.0f));
                              } else {
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim",
                                                                         tr("Could not read that file as an impulse response."));
                              }
                          });
}

// The product mark: a triode symbol (envelope, plate, dashed grid, cathode) in the accent colour, and the name beside it.
void SasEditor::drawLogo(juce::Graphics& g, juce::Rectangle<float> r) const {
    const float size = std::min(r.getHeight(), 38.0f);
    const auto c = juce::Point<float>(r.getX() + size * 0.5f + 2.0f, r.getCentreY());
    const float u = size / 38.0f;  // the layout below is made for a 38 px box
    const auto accent = look_.accent();

    // The product icon (a glowing tube on a dark rounded square), from the same artwork as the application icon.
    static const juce::Image icon = juce::ImageCache::getFromMemory(IconData::icon256_png, IconData::icon256_pngSize);
    if (icon.isValid()) {
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(icon, juce::Rectangle<float>(c.x - size * 0.5f, c.y - size * 0.5f, size, size), juce::RectanglePlacement::centred);
    }

    const float tx = c.x + 23.0f * u;
    g.setColour(look_.text());
    g.setFont(AmpLookAndFeel::font(14.0f, true, 0.14f));
    g.drawText("SCHEMATIC", juce::Rectangle<float>(tx, r.getY() + 3.0f, r.getRight() - tx, 16.0f), juce::Justification::centredLeft);
    g.setColour(accent);
    g.setFont(AmpLookAndFeel::font(14.0f, true, 0.30f));
    g.drawText("AMP SIM", juce::Rectangle<float>(tx, r.getY() + 19.0f, r.getRight() - tx, 16.0f), juce::Justification::centredLeft);
}

void SasEditor::paint(juce::Graphics& g) {
    const auto bg = look_.background();
    g.setGradientFill(juce::ColourGradient(bg.brighter(0.18f), 0.0f, 0.0f, bg.darker(0.35f), 0.0f, float(getHeight()), false));
    g.fillAll();
    // A soft glow of the accent colour behind the title.
    g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.07f), float(getWidth()) * 0.35f, 0.0f, juce::Colours::transparentBlack,
                                           float(getWidth()) * 0.35f, 120.0f, true));
    g.fillRect(0, 0, getWidth(), 130);

    paintPanelBoxes(g, look_, panels_);

    g.setColour(look_.label());
    g.setFont(AmpLookAndFeel::font(10.0f, true, 0.1f));
    g.drawText(tr("IN"), headInLabel_, juce::Justification::centredRight);
    g.drawText(tr("OUT"), headOutLabel_, juce::Justification::centredRight);
    {  // what the modules cost: amber from 70 % of a block, red past it
        const float cpu = totalCpu();
        g.setColour(cpu > 90.0f ? juce::Colour(0xffe64632) : cpu > 70.0f ? look_.accent() : look_.label());
        g.drawText("CPU " + juce::String(int(cpu + 0.5f)) + "%", cpuBounds_, juce::Justification::centredRight);
    }
    drawLogo(g, logoBounds_.toFloat());
    g.setColour(look_.label().withAlpha(0.65f));
    g.setFont(AmpLookAndFeel::font(10.0f));
    g.drawText(juce::String("v") + sas::engineVersion(), 12, getHeight() - 24, 168, 14, juce::Justification::centredLeft);

    // Title of the amp, with a thin accent line under it.
    const int left = 12 + 176;
    g.setColour(look_.accent());
    g.setFont(AmpLookAndFeel::font(26.0f, true, 0.02f));
    g.drawFittedText(title_, titleBounds_.isEmpty() ? juce::Rectangle<int>(left, 12, getWidth() - left - 12 - 160, 34) : titleBounds_, juce::Justification::centredLeft, 1, 0.5f);
    const float lineEnd = titleBounds_.isEmpty() ? float(getWidth() - 12) : float(titleBounds_.getRight());  // it stops where the level meters begin
    g.setGradientFill(juce::ColourGradient(look_.accent().withAlpha(0.7f), float(left), 0.0f, look_.accent().withAlpha(0.0f), lineEnd, 0.0f, false));
    g.fillRect(float(left), 49.0f, std::max(0.0f, lineEnd - float(left)), 1.0f);
}

float SasEditor::totalCpu() const {
    float total = 0.0f;
    const auto& order = processor_.rackOrder();
    for (int i = 0; i < order.count; ++i) total += processor_.rack().moduleCpu(order.slots[std::size_t(i)]);
    return total;
}

void SasEditor::parentHierarchyChanged() {
    // The standalone application's window only has minimise and close; a rack with many modules wants the whole screen.
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) {
        window->setTitleBarButtonsRequired(juce::DocumentWindow::allButtons, false);
        window->setResizable(true, false);
    }
}

void SasEditor::resized() {
    if (getPeer() != nullptr) {  // on screen (not a test or a host that has not shown it yet): remember the size once the drag stops
        sizeDirty_ = true;
        sizeChangedMs_ = juce::Time::getMillisecondCounter();
    }
    constexpr int pad = 12, gap = 8, leftW = 168;
    panels_.clear();
    auto area = getLocalBounds().reduced(pad);

    // Left column: the amp list, jack, quality.
    {
        auto col = area.removeFromLeft(leftW);
        area.removeFromLeft(gap);
        logoBounds_ = col.removeFromTop(40);
        col.removeFromTop(10);
        ampCaption_.setVisible(false);  // the logo and the list say what this is
        ampSearch_.setBounds(col.removeFromTop(24));
        col.removeFromTop(4);
        ampList_.setBounds(col.removeFromTop(std::max(5 * 26 + 14, col.getHeight() - 174)));  // the room the controls below leave: it scrolls when the amps do not fit
        col.removeFromTop(gap);
        jackCaption_.setBounds(col.removeFromTop(16));
        jack_.setBounds(col.removeFromTop(24));
        const bool hasJacks = shownAmp_ >= 0 && !processor_.amps()[std::size_t(shownAmp_)].preamp.jacks.empty();  // the NAM loader has none
        jackCaption_.setVisible(hasJacks);
        jack_.setVisible(hasJacks);
        col.removeFromTop(4);
        if (vibrato_.isVisible()) vibrato_.setBounds(col.removeFromTop(22));
        col.removeFromTop(4);
        qualityCaption_.setBounds(col.removeFromTop(16));
        quality_.setBounds(col.removeFromTop(24));
        col.removeFromTop(6);
        presets_.setBounds(col.removeFromTop(24));
    }

    // Right: title and page tabs, then the page.
    {
        auto head = area.removeFromTop(70);  // tall enough for the level knobs
        // The tabs are as wide as their texts need in the language in use ("Stimmgerät", "Accordeur"), at least what they always were.
        circuitTab_.setBounds(head.removeFromRight(std::max(58, circuitTab_.getBestWidthForHeight(28))).withTrimmedTop(6).withHeight(28));
        head.removeFromRight(4);
        tunerTab_.setBounds(head.removeFromRight(std::max(58, tunerTab_.getBestWidthForHeight(28))).withTrimmedTop(6).withHeight(28));
        head.removeFromRight(4);
        rackTab_.setBounds(head.removeFromRight(std::max(54, rackTab_.getBestWidthForHeight(28))).withTrimmedTop(6).withHeight(28));
        head.removeFromRight(14);
        // The meters and the level knobs: always there, whatever the page. From the right: the cost, then out (bar, knob, label), then in.
        const int row = head.getCentreY();
        constexpr int kKnob = 66;
        // The amp's name has the left of the header. In a narrow window the level bars give up width (down to a stub) until the name fits; if it still
        // does not, the name is squeezed (see paint).
        auto groupWidth = [](int bar) { return 74 + 2 * (bar + 4 + kKnob + 4 + 32) + 14; };
        juce::GlyphArrangement titleGlyphs;
        titleGlyphs.addLineOfText(juce::Font(AmpLookAndFeel::font(26.0f, true, 0.02f)), title_, 0.0f, 0.0f);
        const int need = int(titleGlyphs.getBoundingBox(0, -1, true).getWidth()) + 24;
        int kBar = 104;
        while (kBar > 24 && head.getWidth() - groupWidth(kBar) < need) kBar -= 8;
        titleBounds_ = {head.getX(), 12, std::max(40, head.getWidth() - groupWidth(kBar) - 6), 34};
        int x = head.getRight();
        x -= 74;
        cpuBounds_ = {x, row - 8, 70, 16};
        x -= kBar + 4;
        headOut_.setBounds(x, row - 7, kBar, 14);
        x -= kKnob + 4;
        headOutKnob_.setBounds(x, row - kKnob / 2, kKnob, kKnob);
        x -= 32;
        headOutLabel_ = {x, row - 8, 28, 16};
        headOutAuto_.setBounds(x - 4, row + 10, 38, 17);
        x -= 14;
        x -= kBar + 4;
        headIn_.setBounds(x, row - 7, kBar, 14);
        x -= kKnob + 4;
        headInKnob_.setBounds(x, row - kKnob / 2, kKnob, kKnob);
        x -= 32;
        headInLabel_ = {x, row - 8, 28, 16};
        headInAuto_.setBounds(x - 4, row + 10, 38, 17);
        area.removeFromTop(4);
    }
    rackPage_->setVisible(page_ == 0);
    if (page_ == 0) rackPage_->setBounds(area);
    circuitPage_->setVisible(page_ == 2);
    if (page_ == 2) circuitPage_->setBounds(area);
    for (auto* c : std::initializer_list<juce::Component*>{&tunerDisplay_, &tunerOn_, &tunerMute_, &tunerRef_, &tunerSens_, &tunerTuning_, &tunerTuningCaption_}) c->setVisible(page_ == 1);
    if (page_ == 1) {
        // Tuner page: the display fills the page, the switches and the reference pitch sit in a column on the right.
        auto side = area.removeFromRight(200);
        area.removeFromRight(gap);
        panels_.push_back({area, tr("CHROMATIC TUNER")});
        tunerDisplay_.setBounds(area.reduced(10, 8).withTrimmedTop(14));
        panels_.push_back({side, tr("TUNER")});
        auto in = side.reduced(10, 6);
        in.removeFromTop(20);
        tunerOn_.setBounds(in.removeFromTop(26));
        in.removeFromTop(4);
        tunerMute_.setBounds(in.removeFromTop(26));
        in.removeFromTop(8);
        tunerTuningCaption_.setBounds(in.removeFromTop(14));
        tunerTuning_.setBounds(in.removeFromTop(26));
        in.removeFromTop(6);
        tunerRef_.setBounds(in.removeFromTop(104));
        in.removeFromTop(4);
        tunerSens_.setBounds(in.removeFromTop(104));
    }
    repaint();
}

int SasEditor::ampTopHeight() const {
    if (face_) return 300;
    const bool grouped = shownAmp_ >= 0 && shownAmp_ < int(processor_.amps().size()) && !processor_.amps()[std::size_t(shownAmp_)].ui.empty() &&
                         !processor_.amps()[std::size_t(shownAmp_)].ui.front().group.empty();
    // The NAM loader's twelve knobs wrap into two rows of blocks.
    const bool wraps = grouped && processor_.amps()[std::size_t(shownAmp_)].ui.size() > 8;
    // Graphic EQ faders need travel: their row gets taller.
    bool faders = false;
    if (shownAmp_ >= 0 && shownAmp_ < int(processor_.amps().size()))
        for (const auto& u : processor_.amps()[std::size_t(shownAmp_)].ui) faders = faders || u.style == "slider";
    return (wraps ? 215 + 120 : grouped ? 215 : 170) + (faders ? 110 : 0);
}

int SasEditor::ampPanelHeight() const { return ampTopHeight() + 8 + 136; }

// The cabinet unit changes shape with its mode: MULTI has the plane and eight strips where the two impulses A and B are otherwise.
void SasEditor::updateCabMode() {
    const bool multi = int(processor_.apvts.getRawParameterValue(ids::cab)->load() + 0.5f) == int(CabMode::Multi);
    if (multi != multiMode_) {
        multiMode_ = multi;
        rackPage_->resized();   // the unit's height changed
        cabPanel_.resized();
    }
    cabNote_.setText(multiMode_ ? tr("MULTI mixes the impulses of the strips with the ring of the plane above; SPREAD sets how far each point reaches.")
                                : tr("IR plays the impulses above, mixed by the blend. Filter is a simple cabinet curve; Off is the raw amp."),
                     juce::dontSendNotification);
    if (multiMode_) {
        int flipped = 0;
        double latest = 0.0;
        bool any = false;
        for (int k = 0; k < SasProcessor::kMultiSlots; ++k) {
            const auto info = processor_.multiAlignInfo(k);
            if (!info.valid) continue;
            any = true;
            flipped += info.flipped ? 1 : 0;
            latest = std::max(latest, info.delayMs);
        }
        multiAlignNote_.setText(any ? juce::String(flipped) + " " + tr("flipped") + ", " + juce::String(latest, 2) + " ms" : juce::String(), juce::dontSendNotification);
    }
}

int SasEditor::cabPanelHeight() const { return (multiMode_ ? MultiPanel::kHeight : 190) + 8 + 164; }  // the blend panel also holds the "in phase" switch and its note

// The amp unit: the amp's front (or its knobs), then the guitar, the tubes and the levels, then the state of the amp itself (docs/MODULAR_RACK.md:
// the condition goes with the amp) and the speaker.
void SasEditor::layoutAmpPanel() {
    constexpr int gap = 8;
    auto& panels = ampPanel_.panels;
    panels.clear();
    auto area = ampPanel_.getLocalBounds();
    const bool namAmp = shownAmp_ >= 0 && processor_.amps()[std::size_t(shownAmp_)].nam;
    auto top = area.removeFromTop(ampTopHeight());
    area.removeFromTop(gap);
    auto mid = area;  // the guitar, the power tubes and the amp's condition share one row

    const int selectors = shownAmp_ >= 0 ? int(processor_.amps()[std::size_t(shownAmp_)].preamp.selectors.size()) : 0;
    for (int i = 0; i < 6; ++i) {
        selBox_[i].setVisible(i < selectors);
        selCaption_[i].setVisible(i < selectors);
    }
    for (auto& k : ampKnobs_) k->setVisible(!face_);
    if (face_) face_->setVisible(true);
    for (auto* c : std::initializer_list<juce::Component*>{&namLoad_, &namClear_, &namName_, &namNote_}) c->setVisible(namAmp);
    if (face_) {
        // An amp with a faceplate: its front fills the panel; its own switches (if any) go in a strip under it.
        auto faceArea = top;
        if (selectors > 0) {
            auto strip = faceArea.removeFromBottom(46);
            const int w = strip.getWidth() / selectors;
            for (int i = 0; i < selectors; ++i) {
                auto cell = strip.removeFromLeft(w).reduced(6, 2);
                selCaption_[i].setBounds(cell.removeFromTop(16));
                selBox_[i].setBounds(cell.removeFromTop(24));
            }
        }
        face_->setBounds(faceArea);
    } else {
        panels.push_back({top, tr("AMP CONTROLS")});
        auto row = top.reduced(6, 4);
        row.removeFromTop(16);
        if (selectors > 0) {
            // The amp's own switches, in a strip along the bottom of the panel.
            auto strip = row.removeFromBottom(44);
            const int w = strip.getWidth() / selectors;
            for (int i = 0; i < selectors; ++i) {
                auto cell = strip.removeFromLeft(w).reduced(6, 0);
                selCaption_[i].setBounds(cell.removeFromTop(16));
                selBox_[i].setBounds(cell.removeFromTop(24));
            }
        }
        if (namAmp) {
            // The NAM loader: the model's file and name along the bottom, the DRIVE and LEVEL knobs above.
            auto strip = row.removeFromBottom(58);
            auto buttons = strip.removeFromLeft(150);
            namLoad_.setBounds(buttons.removeFromTop(26));
            buttons.removeFromTop(4);
            namClear_.setBounds(buttons.removeFromTop(24).removeFromLeft(70));
            strip.removeFromLeft(10);
            namName_.setBounds(strip.removeFromTop(24));
            namNote_.setBounds(strip);
        }
        const auto& ui = shownAmp_ >= 0 ? processor_.amps()[std::size_t(shownAmp_)].ui : std::vector<sas::UiKnob>{};
        const bool grouped = !ui.empty() && !ui.front().group.empty();
        if (grouped) {
            // Neighbouring knobs with the same "group" share a block with its title; a block is as wide as its knobs.
            // With many knobs (the NAM loader has 12) the blocks wrap into two rows, split where the counts are most even.
            const std::size_t total = std::min(ampKnobs_.size(), ui.size());
            std::size_t split = total;  // the first knob of the second row
            if (total > 8) {
                std::size_t best = total;
                for (std::size_t i = 1; i < total; ++i) {
                    if (ui[i].group == ui[i - 1].group) continue;
                    const std::size_t diff = i > total - i ? i - (total - i) : (total - i) - i;
                    if (best == total || diff < (best > total - best ? best - (total - best) : (total - best) - best)) best = i;
                }
                split = best;
            }
            const bool twoRows = split < total;
            const int widest = twoRows ? int(std::max(split, total - split)) : int(std::max<std::size_t>(1, ampKnobs_.size()));
            const int w = row.getWidth() / widest;  // both rows use the same cell width so the knobs line up
            // The row that holds graphic EQ faders gets 60 % of the height (they need travel), else the rows share it evenly.
            auto rowHasFader = [&](std::size_t from, std::size_t to) {
                for (std::size_t q = from; q < to; ++q) if (ui[q].style == "slider") return true;
                return false;
            };
            const bool topFaders = twoRows && rowHasFader(0, split), bottomFaders = twoRows && rowHasFader(split, total);
            const int topShare = topFaders == bottomFaders ? 50 : topFaders ? 60 : 40;
            auto rowArea = twoRows ? row.removeFromTop(row.getHeight() * topShare / 100) : row;
            for (std::size_t i = 0; i < total;) {
                if (i == split) rowArea = row;
                std::size_t j = i;
                while (j < total && ui[j].group == ui[i].group) ++j;
                auto block = rowArea.removeFromLeft(w * int(j - i));
                panels.push_back({block.expanded(-1, 2), ui[i].group});
                auto inner = block.withTrimmedTop(20);
                for (std::size_t k = i; k < j; ++k) ampKnobs_[k]->setBounds(inner.removeFromLeft(w));
                i = j;
            }
        } else {
            const int n = std::max<int>(1, int(ampKnobs_.size()));
            const int w = row.getWidth() / n;
            for (auto& k : ampKnobs_) k->setBounds(row.removeFromLeft(w));
        }
    }

    // Middle row: the guitar and the power tubes (the input and output levels are in the header).
    auto guitarPanel = mid.removeFromLeft(190);
    mid.removeFromLeft(gap);
    panels.push_back({guitarPanel, tr("GUITAR INPUT")});
    {
        auto gin = guitarPanel.reduced(8, 4);
        gin.removeFromTop(18);
        cable_.setBounds(gin.removeFromRight(70));
        gin.removeFromRight(4);
        pickupCaption_.setBounds(gin.removeFromTop(16));
        pickup_.setBounds(gin.removeFromTop(24));
    }
    if (!namAmp) {
        auto tubesPanel = mid.removeFromLeft(std::clamp(mid.getWidth() * 28 / 100, 200, 290));
        mid.removeFromLeft(gap);
        const bool transistors = shownAmp_ >= 0 && shownAmp_ < int(processor_.amps().size()) && processor_.amps()[std::size_t(shownAmp_)].power.tubes.empty();
        panels.push_back({tubesPanel, transistors ? tr("POWER TRANSISTORS") : tr("POWER TUBES")});
        tubes_.setBounds(tubesPanel.reduced(8, 4).withTrimmedTop(18));
    }
    tubes_.setVisible(!namAmp);

    // The amp's condition fills the rest of the row (the speaker's dynamics are with the cabinet).
    auto condPanel = mid;
    panels.push_back({condPanel, tr("AMP CONDITION")});
    {
        const int condKnobW = 75;
        auto cin = condPanel.reduced(8, 4);
        cin.removeFromTop(18);
        conditionNote_.setBounds(cin.removeFromBottom(16));
        // An amp with a transistor output stage has no power tubes to pick and no bias to set: those controls are not shown.
        const bool solidState = shownAmp_ >= 0 && shownAmp_ < int(processor_.amps().size()) && processor_.amps()[std::size_t(shownAmp_)].power.tubes.empty();
        for (auto* c : std::initializer_list<juce::Component*>{&tubeCaption_, &tube_, &idleLabel_, &bias_}) c->setVisible(!solidState);
        if (!solidState) {
            auto col1 = cin.removeFromLeft(130);
            tubeCaption_.setBounds(col1.removeFromTop(16));
            tube_.setBounds(col1.removeFromTop(24));
            idleLabel_.setBounds(col1.removeFromTop(20));
            bias_.setBounds(cin.removeFromLeft(condKnobW));
        }
        sag_.setBounds(cin.removeFromLeft(condKnobW));
        auto col2 = cin.removeFromLeft(120);
        col2.removeFromLeft(10);
        noiseCaption_.setBounds(col2.removeFromTop(16));
        noise_.setBounds(col2.removeFromTop(24));
        wear_.setBounds(cin.removeFromLeft(condKnobW));
        auto col3 = cin.removeFromLeft(110);
        unitCaption_.setBounds(col3.removeFromTop(16));
        unit_.setBounds(col3.removeFromTop(24));
    }
}

// The cabinet unit: the two impulse slots on top, each with its response curve; below, the cabinet mode, the A / B blend and the stereo.
void SasEditor::layoutCabPanel() {
    constexpr int gap = 8;
    auto& panels = cabPanel_.panels;
    panels.clear();
    auto area = cabPanel_.getLocalBounds();
    const bool multi = multiMode_;
    for (auto* c : std::initializer_list<juce::Component*>{&loadA_, &clearA_, &loadB_, &clearB_, &nameA_, &nameB_, &curveA_, &curveB_, &blend_, &blendNote_, &alignOn_, &alignNote_})
        c->setVisible(!multi);
    for (auto* c : std::initializer_list<juce::Component*>{multiPanel_.get(), multiSpread_.get(), &multiAlign_, &multiAlignNote_}) c->setVisible(multi);
    auto top = area.removeFromTop(multi ? MultiPanel::kHeight : 190);
    area.removeFromTop(gap);
    auto slotA = top.removeFromLeft((top.getWidth() - gap) / 2);
    top.removeFromLeft(gap);
    auto slotB = top;
    auto slot = [&](juce::Rectangle<int> panel, const juce::String& caption, juce::TextButton& load, juce::TextButton& clear, juce::Label& name, IrCurve& curve) {
        panels.push_back({panel, caption});
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
    if (multi) {
        multiPanel_->setBounds(cabPanel_.getLocalBounds().removeFromTop(MultiPanel::kHeight));
    } else {
        slot(slotA, tr("IMPULSE A"), loadA_, clearA_, nameA_, curveA_);
        slot(slotB, tr("IMPULSE B"), loadB_, clearB_, nameB_, curveB_);
    }

    auto cabPanel = area.removeFromLeft(200);
    area.removeFromLeft(gap);
    auto blendPanel = area.removeFromLeft(96);
    area.removeFromLeft(gap);
    auto speakerPanel = area.removeFromLeft(118);
    area.removeFromLeft(gap);
    auto stereoPanel = area;
    panels.push_back({cabPanel, tr("CABINET")});
    {
        auto in = cabPanel.reduced(10, 6);
        in.removeFromTop(20);
        cabCaption_.setBounds(in.removeFromTop(16));
        cab_.setBounds(in.removeFromTop(24));
        in.removeFromTop(6);
        auto nrow = in.removeFromTop(22);
        namCabClear_.setBounds(nrow.removeFromRight(46));
        nrow.removeFromRight(4);
        namCabLoad_.setBounds(nrow);
        namCabName_.setBounds(in.removeFromTop(18));
        in.removeFromTop(2);
        cabNote_.setBounds(in);
    }
    if (multi) {
        panels.push_back({blendPanel, tr("MULTI")});
        auto in = blendPanel.reduced(6, 4);
        in.removeFromTop(16);
        multiAlignNote_.setBounds(in.removeFromBottom(14));
        multiAlign_.setBounds(in.removeFromBottom(20).withSizeKeepingCentre(84, 20));
        multiSpread_->setBounds(in);
    } else {
        panels.push_back({blendPanel, tr("A / B BLEND")});
        auto in = blendPanel.reduced(6, 4);
        in.removeFromTop(16);
        alignNote_.setBounds(in.removeFromBottom(14));
        alignOn_.setBounds(in.removeFromBottom(20).withSizeKeepingCentre(84, 20));
        blendNote_.setBounds(in.removeFromBottom(14));
        blend_.setBounds(in);
    }
    panels.push_back({speakerPanel, tr("SPEAKER")});
    {
        auto in = speakerPanel.reduced(6, 4);
        in.removeFromTop(16);
        speakerIntense_.setBounds(in.removeFromBottom(20).withSizeKeepingCentre(80, 20));
        speaker_.setBounds(in);
    }
    panels.push_back({stereoPanel, tr("STEREO")});
    {
        auto in = stereoPanel.reduced(6, 4);
        in.removeFromTop(16);
        const int w = in.getWidth() / 3;
        stereoWidth_.setBounds(in.removeFromLeft(w));
        doubleAmount_.setBounds(in.removeFromLeft(w));
        doubleTime_.setBounds(in);
    }
}

