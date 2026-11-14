#include "I18n.h"
#include "ModuleUnit.h"

#include "EqPresets.h"
#include "FxPresets.h"
#include "ModulePresets.h"
#include "RackState.h"

using sas::ModuleType;

namespace {

struct Item {
    enum Kind { Knob, Toggle, Combo } kind;
    const char* id;       // the base id (instance 0)
    const char* caption;
};

const std::vector<Item>& itemsFor(ModuleType t) {
    static const std::vector<Item> none;
    static const std::vector<Item> gate{{Item::Knob, ids::gateThreshold, "THRESHOLD dB"}, {Item::Knob, ids::gateRelease, "RELEASE ms"}};
    static const std::vector<Item> comp{{Item::Combo, ids::compModel, "MODEL"}, {Item::Knob, ids::compSustain, "SUSTAIN"}, {Item::Knob, ids::compBlend, "BLEND"}, {Item::Knob, ids::compTone, "TONE"},
                                        {Item::Knob, ids::compLevel, "LEVEL"}, {Item::Toggle, ids::compFast, "Fast"}};
    static const std::vector<Item> drive{{Item::Combo, ids::tsModel, "MODEL"}, {Item::Knob, ids::tsDrive, "DRIVE"}, {Item::Knob, ids::tsTone, "TONE"}, {Item::Knob, ids::tsLevel, "LEVEL"}};
    static const std::vector<Item> nam{{Item::Knob, ids::nmpDrive, "DRIVE dB"}, {Item::Knob, ids::nmpLevel, "LEVEL dB"}};
    static const std::vector<Item> phaser{{Item::Knob, ids::phSpeed, "SPEED Hz"}, {Item::Toggle, ids::phIntense, "Intense"}, {Item::Knob, ids::phMix, "MIX %"}, {Item::Combo, ids::phStereo, "CHANNELS"}};
    static const std::vector<Item> chorus{{Item::Combo, ids::chMode, "MODE"}, {Item::Knob, ids::chRate, "RATE Hz"}, {Item::Knob, ids::chDepth, "DEPTH %"},
                                          {Item::Combo, ids::chJuno, "JUNO BUTTON"}, {Item::Knob, ids::chMix, "MIX %"}, {Item::Combo, ids::chStereo, "CHANNELS"}};
    static const std::vector<Item> vibe{{Item::Knob, ids::vbSpeed, "SPEED Hz"}, {Item::Knob, ids::vbIntensity, "INTENSITY %"}, {Item::Toggle, ids::vbVibrato, "Vibrato"}, {Item::Knob, ids::vbMix, "MIX %"}, {Item::Combo, ids::vbStereo, "CHANNELS"}};
    static const std::vector<Item> tremolo{{Item::Knob, ids::trRate, "RATE Hz"}, {Item::Knob, ids::trDepth, "DEPTH %"}, {Item::Combo, ids::trShape, "SHAPE"},
                                           {Item::Combo, ids::trStereo, "CHANNELS"}};
    static const std::vector<Item> flanger{{Item::Knob, ids::flRate, "RATE Hz"}, {Item::Knob, ids::flDepth, "DEPTH %"}, {Item::Knob, ids::flManual, "MANUAL %"},
                                           {Item::Knob, ids::flFeedback, "FEEDBACK %"}, {Item::Knob, ids::flMix, "MIX %"}, {Item::Combo, ids::flStereo, "CHANNELS"}};
    static const std::vector<Item> fuzz{{Item::Combo, ids::fzType, "TYPE"}, {Item::Knob, ids::fzFuzz, "FUZZ"}, {Item::Knob, ids::fzTone, "TONE"},
                                        {Item::Knob, ids::fzBias, "BIAS"}, {Item::Knob, ids::fzMix, "MIX %"}, {Item::Knob, ids::fzLevel, "LEVEL dB"}};
    static const std::vector<Item> tape{{Item::Knob, ids::tpTime, "TIME ms"}, {Item::Combo, ids::tpHeads, "HEADS"}, {Item::Knob, ids::tpIntensity, "INTENSITY %"},
                                        {Item::Knob, ids::tpMix, "MIX %"}, {Item::Knob, ids::tpTone, "TONE Hz"}, {Item::Knob, ids::tpWow, "WOW %"},
                                        {Item::Knob, ids::tpFlutter, "FLUTTER %"}, {Item::Knob, ids::tpSat, "SATURATION %"}, {Item::Combo, ids::tpStereo, "CHANNELS"}};
    static const std::vector<Item> bbd{{Item::Knob, ids::bbdTime, "TIME ms"}, {Item::Knob, ids::bbdFeedback, "FEEDBACK %"}, {Item::Knob, ids::bbdMix, "MIX %"},
                                       {Item::Knob, ids::bbdTone, "TONE Hz"}, {Item::Knob, ids::bbdRate, "MOD RATE Hz"}, {Item::Knob, ids::bbdDepth, "MOD DEPTH %"},
                                       {Item::Knob, ids::bbdGrit, "GRIT %"}, {Item::Combo, ids::bbdStereo, "CHANNELS"}};
    static const std::vector<Item> crusher{{Item::Knob, ids::bcBits, "BITS"}, {Item::Knob, ids::bcRate, "RATE Hz"}, {Item::Toggle, ids::bcAntiAlias, "Anti-alias"},
                                           {Item::Knob, ids::bcTone, "TONE Hz"}, {Item::Knob, ids::bcDither, "DITHER %"}, {Item::Knob, ids::bcJitter, "JITTER %"},
                                           {Item::Knob, ids::bcMix, "MIX %"}};
    static const std::vector<Item> layers{{Item::Knob, ids::lyMix, "MIX"}, {Item::Knob, ids::lyAttack, "ATTACK"}, {Item::Knob, ids::lyDecay, "DECAY"},
                                          {Item::Knob, ids::lyTrig, "TRIG"}, {Item::Knob, ids::lyDiffusion, "DIFFUSION"}, {Item::Toggle, ids::lySingle, "Single"},
                                          {Item::Toggle, ids::lySub, "Sub octave"}, {Item::Toggle, ids::lyHold, "Hold layer"}, {Item::Combo, ids::lyStereo, "CHANNELS"}};
    static const std::vector<Item> distortion{{Item::Knob, ids::dstDrive, "DIST"}, {Item::Knob, ids::dstTone, "TONE"}, {Item::Knob, ids::dstLevel, "LEVEL"}};
    static const std::vector<Item> leslie{{Item::Combo, ids::lesSpeed, "SPEED"}, {Item::Knob, ids::lesDepth, "DEPTH"}, {Item::Knob, ids::lesBalance, "HORN / ROTOR"}, {Item::Knob, ids::lesWidth, "WIDTH"}, {Item::Knob, ids::lesMix, "MIX %"}};
    static const std::vector<Item> spring{{Item::Combo, ids::spType, "TANK"}, {Item::Knob, ids::spTone, "TONE"}, {Item::Knob, ids::spMix, "MIX %"}};
    static const std::vector<Item> klon{{Item::Knob, ids::klnGain, "GAIN"}, {Item::Knob, ids::klnTreble, "TREBLE"}, {Item::Knob, ids::klnLevel, "LEVEL"}};
    static const std::vector<Item> acoustic{{Item::Combo, ids::acType, "BODY TYPE"}, {Item::Knob, ids::acBody, "BODY"}, {Item::Knob, ids::acTop, "TOP"},
                                            {Item::Knob, ids::acAir, "AIR"}, {Item::Knob, ids::acSquash, "SQUASH"}, {Item::Knob, ids::acMix, "MIX %"}};
    static const std::vector<Item> harmonist{{Item::Combo, ids::harMode, "MODE"}, {Item::Combo, ids::harKey, "KEY"}, {Item::Combo, ids::harScale, "SCALE"},
                                             {Item::Knob, ids::harInt1, "INTERVAL 1"}, {Item::Knob, ids::harInt2, "INTERVAL 2"}, {Item::Knob, ids::harDetune, "DETUNE cents"},
                                             {Item::Knob, ids::harDirect, "DIRECT"}, {Item::Knob, ids::harVoice1, "VOICE 1"}, {Item::Knob, ids::harVoice2, "VOICE 2"}};
    static const std::vector<Item> octaver{{Item::Knob, ids::octDirect, "DIRECT"}, {Item::Knob, ids::octOct1, "OCTAVE 1"}, {Item::Knob, ids::octOct2, "OCTAVE 2"}, {Item::Knob, ids::octTone, "TONE"}};
    static const std::vector<Item> boost{{Item::Knob, ids::bstGain, "GAIN dB"}, {Item::Knob, ids::bstTone, "TONE"}, {Item::Toggle, ids::bstSoft, "Soft clip"}};
    static const std::vector<Item> limiter{{Item::Knob, ids::limGain, "GAIN dB"}, {Item::Knob, ids::limCeiling, "CEILING dB"}, {Item::Knob, ids::limRelease, "RELEASE ms"}};
    static const std::vector<Item> wah{{Item::Combo, ids::wahMode, "MODE"}, {Item::Combo, ids::wahFilter, "FILTER"}, {Item::Knob, ids::wahPos, "PEDAL %"}, {Item::Knob, ids::wahHeel, "HEEL Hz"},
                                       {Item::Knob, ids::wahToe, "TOE Hz"}, {Item::Knob, ids::wahQ, "Q"}, {Item::Knob, ids::wahMix, "MIX %"},
                                       {Item::Knob, ids::wahLevel, "LEVEL dB"}, {Item::Knob, ids::wahSens, "ENV SENS %"}, {Item::Knob, ids::wahAttack, "ENV ATTACK ms"},
                                       {Item::Knob, ids::wahRelease, "ENV RELEASE ms"}, {Item::Combo, ids::wahDir, "ENV DIRECTION"}, {Item::Knob, ids::wahRate, "LFO RATE Hz"},
                                       {Item::Combo, ids::wahStereo, "CHANNELS"}};
    static const std::vector<Item> pitch{{Item::Knob, ids::pitch, "SEMITONES"}, {Item::Knob, ids::pitchFine, "FINE cents"}};
    static const std::vector<Item> delay{{Item::Knob, ids::dlyTime, "TIME ms"}, {Item::Combo, ids::dlySync, "SYNC"}, {Item::Knob, ids::dlyFeedback, "FEEDBACK %"},
                                         {Item::Knob, ids::dlyMix, "MIX %"}, {Item::Knob, ids::dlyTone, "TONE Hz"}, {Item::Toggle, ids::dlyPing, "Ping-pong"},
                                         {Item::Toggle, ids::dlyAnalog, "Analog"}, {Item::Combo, ids::dlyStereo, "CHANNELS"}};
    static const std::vector<Item> reverb{{Item::Toggle, ids::plHall, "Hall"}, {Item::Knob, ids::plDecay, "DECAY s"}, {Item::Knob, ids::plPre, "PRE-DELAY ms"},
                                          {Item::Knob, ids::plTone, "TONE Hz"}, {Item::Knob, ids::plLow, "LOW CUT Hz"}, {Item::Knob, ids::plMix, "MIX %"},
                                          {Item::Knob, ids::plMod, "MOD %"}, {Item::Knob, ids::plShimmer, "SHIMMER %"}, {Item::Combo, ids::plShimmerInt, "SHIMMER INTERVAL"}, {Item::Combo, ids::plStereo, "CHANNELS"}};
    static const std::vector<Item> mixer{{Item::Knob, ids::mixLevelA, "LEVEL A dB"}, {Item::Knob, ids::mixLevelB, "LEVEL B dB"}, {Item::Knob, ids::mixPanA, "PAN A"},
                                         {Item::Knob, ids::mixPanB, "PAN B"}, {Item::Toggle, ids::mixInvertB, "Invert B"}};
    switch (t) {
        case ModuleType::Mixer: return mixer;
        case ModuleType::Gate: return gate;
        case ModuleType::Compressor: return comp;
        case ModuleType::Overdrive: return drive;
        case ModuleType::NamPedal: return nam;
        case ModuleType::Phaser: return phaser;
        case ModuleType::Chorus: return chorus;
        case ModuleType::Vibe: return vibe;
        case ModuleType::Tremolo: return tremolo;
        case ModuleType::Flanger: return flanger;
        case ModuleType::Wah: return wah;
        case ModuleType::Fuzz: return fuzz;
        case ModuleType::Boost: return boost;
        case ModuleType::Octaver: return octaver;
        case ModuleType::Harmonist: return harmonist;
        case ModuleType::Acoustic: return acoustic;
        case ModuleType::Klon: return klon;
        case ModuleType::Leslie: return leslie;
        case ModuleType::SpringReverb: return spring;
        case ModuleType::TapeEcho: return tape;
        case ModuleType::BbdDelay: return bbd;
        case ModuleType::Bitcrusher: return crusher;
        case ModuleType::Layers: return layers;
        case ModuleType::Distortion: return distortion;
        case ModuleType::Limiter: return limiter;
        case ModuleType::Pitch: return pitch;
        case ModuleType::Delay: return delay;
        case ModuleType::Reverb: return reverb;
        default: return none;
    }
}

const char* nameOf(ModuleType t) {
    switch (t) {
        case ModuleType::Amp: return "AMP";
        case ModuleType::Cabinet: return "CABINET";
        case ModuleType::Gate: return "NOISE GATE";
        case ModuleType::Compressor: return "COMPRESSOR";
        case ModuleType::Overdrive: return "OVERDRIVE";
        case ModuleType::NamPedal: return "NAM PEDAL";
        case ModuleType::Phaser: return "PHASER";
        case ModuleType::Chorus: return "CHORUS";
        case ModuleType::Vibe: return "VIBE";
        case ModuleType::Tremolo: return "TREMOLO";
        case ModuleType::Flanger: return "FLANGER";
        case ModuleType::Wah: return "WAH";
        case ModuleType::Fuzz: return "FUZZ";
        case ModuleType::Boost: return "CLEAN BOOST";
        case ModuleType::Octaver: return "ANALOG OCTAVER";
        case ModuleType::Harmonist: return "HARMONIZER";
        case ModuleType::Acoustic: return "ACOUSTIC SIM";
        case ModuleType::Klon: return "TRANSPARENT DRIVE";
        case ModuleType::Leslie: return "ROTARY SPEAKER";
        case ModuleType::SpringReverb: return "SPRING REVERB";
        case ModuleType::TapeEcho: return "TAPE ECHO";
        case ModuleType::BbdDelay: return "ANALOG DELAY (BBD)";
        case ModuleType::Bitcrusher: return "BITCRUSHER";
        case ModuleType::Layers: return "SUSTAIN LAYERS";
        case ModuleType::Distortion: return "DISTORTION";
        case ModuleType::Limiter: return "LIMITER";
        case ModuleType::Pitch: return "PITCH";
        case ModuleType::Eq: return "EQ";
        case ModuleType::TenBandEq: return "TEN-BAND EQ";
        case ModuleType::Delay: return "DELAY";
        case ModuleType::Reverb: return "REVERB";
        case ModuleType::Splitter: return "SPLITTER";
        case ModuleType::Mixer: return "MIXER";
    }
    return "";
}

const AmpLookAndFeel* lookOf(const juce::Component& c) { return dynamic_cast<const AmpLookAndFeel*>(&c.getLookAndFeel()); }

// Knobs, switches and choices of one instance in a row.
class ParamBody final : public ModuleUnit::Body {
public:
    ParamBody(SasProcessor& p, sas::SlotRef slot) {
        for (const auto& item : itemsFor(slot.type)) {
            const juce::String id = RackState::paramId(item.id, slot.instance);
            switch (item.kind) {
                case Item::Knob: {
                    auto k = std::make_unique<KnobWithLabel>(p.apvts, id.toRawUTF8(), item.caption);
                    addAndMakeVisible(*k);
                    parts_.push_back({k.get(), 66, item.id});
                    knobs_.push_back(std::move(k));
                    break;
                }
                case Item::Toggle: {
                    auto t = std::make_unique<juce::ToggleButton>(tr(item.caption));
                    addAndMakeVisible(*t);
                    toggleAtts_.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, id, *t));
                    parts_.push_back({t.get(), 92, item.id});
                    toggles_.push_back(std::move(t));
                    break;
                }
                case Item::Combo: {
                    auto holder = std::make_unique<Choice>();
                    holder->caption.setText(tr(item.caption), juce::dontSendNotification);
                    holder->caption.setFont(AmpLookAndFeel::font(10.5f, true, 0.12f));
                    holder->addAndMakeVisible(holder->caption);
                    holder->addAndMakeVisible(holder->box);
                    const auto* param = dynamic_cast<juce::AudioParameterChoice*>(p.apvts.getParameter(id));
                    holder->box.addItemList(trAll(param->choices), 1);
                    comboAtts_.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, id, holder->box));
                    addAndMakeVisible(*holder);
                    parts_.push_back({holder.get(), slot.type == sas::ModuleType::Wah ? 108 : 124, item.id});
                    if (item.id == std::string_view(ids::wahMode)) {
                        wahMode_ = &holder->box;
                        holder->box.onChange = [this] { resized(); };  // the controls of the other modes go away
                    }
                    choices_.push_back(std::move(holder));
                    break;
                }
            }
        }
    }

    int height() const override { return 100; }

    void resized() override {
        auto r = getLocalBounds().reduced(6, 2);
        for (auto& part : parts_) {
            part.c->setVisible(shown(part.id));
            if (!shown(part.id)) continue;
            auto cell = r.removeFromLeft(part.width);
            if (dynamic_cast<juce::ToggleButton*>(part.c)) part.c->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
            else if (dynamic_cast<Choice*>(part.c)) part.c->setBounds(cell.withSizeKeepingCentre(cell.getWidth() - 6, 46).translated(0, -6));
            else part.c->setBounds(cell);
        }
    }

private:
    // The wah shows what its mode uses: the pedal, the envelope follower or the LFO.
    bool shown(std::string_view id) const {
        if (wahMode_ == nullptr) return true;
        const int mode = std::max(0, wahMode_->getSelectedItemIndex());
        const bool env = id == ids::wahSens || id == ids::wahAttack || id == ids::wahRelease || id == ids::wahDir;
        if (env) return mode == 1;
        if (id == ids::wahPos) return mode == 0;
        if (id == ids::wahRate) return mode == 2;
        return true;
    }
    struct Choice final : juce::Component {
        void resized() override {
            auto r = getLocalBounds();
            caption.setBounds(r.removeFromTop(18));
            box.setBounds(r.removeFromTop(26));
        }
        juce::Label caption;
        juce::ComboBox box;
    };
    struct Part {
        juce::Component* c;
        int width;
        std::string_view id;
    };
    juce::ComboBox* wahMode_ = nullptr;
    std::vector<Part> parts_;
    std::vector<std::unique_ptr<KnobWithLabel>> knobs_;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAtts_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts_;
};

// The equaliser: the curve with its draggable bands, the knobs of the selected band, the high-pass and the low-pass.
class EqBody final : public ModuleUnit::Body {
public:
    EqBody(SasProcessor& p, sas::SlotRef slot) : processor_(p), curve_(p.apvts, slot.instance) {
        addAndMakeVisible(curve_);
        hpf_ = std::make_unique<KnobWithLabel>(p.apvts, RackState::paramId(ids::eqHpf, slot.instance).toRawUTF8(), "HIGH-PASS");
        lpf_ = std::make_unique<KnobWithLabel>(p.apvts, RackState::paramId(ids::eqLpf, slot.instance).toRawUTF8(), "LOW-PASS");
        addAndMakeVisible(*hpf_);
        addAndMakeVisible(*lpf_);
        for (int b = 0; b < sas::EqParams::kBands; ++b) {
            const juce::String caption[3] = {tr("FREQ Hz"), tr("GAIN dB"), "Q"};
            const char* base[3] = {ids::eqFreq[b], ids::eqGain[b], ids::eqQ[b]};
            for (int k = 0; k < 3; ++k) {
                knobs_[b][k] = std::make_unique<KnobWithLabel>(p.apvts, RackState::paramId(base[k], slot.instance).toRawUTF8(), caption[k]);
                addChildComponent(*knobs_[b][k]);
            }
        }
        band_.setFont(AmpLookAndFeel::font(13.0f, true, 0.05f));
        addAndMakeVisible(band_);
        curve_.onSelect = [this](int b) { select(b); };
        select(curve_.selected());
    }

    int height() const override { return 250; }

    // What is sounding, in a silhouette behind the curve (the output of the whole rack).
    void tick() override {
        if (!isShowing()) return;
        processor_.spectrumFeed().latest(buffer_.data(), buffer_.size());
        spectrum_.setSampleRate(processor_.getSampleRate() > 0.0 ? processor_.getSampleRate() : 48000.0);
        spectrum_.analyze(buffer_.data());
        curve_.setSpectrum(&spectrum_.db());
        curve_.repaint();  // it follows the parameters too, when the host or an automation moves them
    }

    void resized() override {
        auto r = getLocalBounds().reduced(6, 2);
        auto left = r.removeFromLeft(r.getWidth() - 3 * 78);
        auto side = r;
        curve_.setBounds(left.withTrimmedBottom(0));
        band_.setBounds(side.removeFromTop(20));
        auto row = side.removeFromTop(100);
        for (int k = 0; k < 3; ++k) {
            auto cell = row.removeFromLeft(78);
            for (int b = 0; b < sas::EqParams::kBands; ++b) knobs_[b][k]->setBounds(cell);
        }
        auto filters = side.removeFromTop(100);
        hpf_->setBounds(filters.removeFromLeft(78));
        lpf_->setBounds(filters.removeFromLeft(78));
    }

private:
    void select(int band) {
        for (int b = 0; b < sas::EqParams::kBands; ++b)
            for (auto& k : knobs_[b]) k->setVisible(b == band);
        band_.setText(tr("BAND ") + juce::String(band + 1), juce::dontSendNotification);
        resized();
    }
    SasProcessor& processor_;
    sas::SpectrumAnalyzer spectrum_;
    std::vector<float> buffer_ = std::vector<float>(sas::SpectrumAnalyzer::kFft);
    EqCurve curve_;
    std::unique_ptr<KnobWithLabel> hpf_, lpf_, knobs_[sas::EqParams::kBands][3];
    juce::Label band_;
};

// The ten-band graphic EQ pedal: ten vertical sliders an octave apart (31 Hz to 16 kHz, +-12 dB, double click for flat) with the GAIN and VOLUME knobs.
class TenBandBody final : public ModuleUnit::Body {
public:
    TenBandBody(SasProcessor& p, sas::SlotRef slot) {
        static const char* const kNames[10] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
        for (int b = 0; b < 10; ++b) {
            auto s = std::make_unique<juce::Slider>(juce::Slider::LinearVertical, juce::Slider::NoTextBox);
            s->setDoubleClickReturnValue(true, 0.0);
            s->setPopupDisplayEnabled(true, true, this);
            addAndMakeVisible(*s);
            atts_.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, RackState::paramId(ids::tenBand[b], slot.instance), *s));
            sliders_.push_back(std::move(s));
            auto l = std::make_unique<juce::Label>();
            l->setText(kNames[b], juce::dontSendNotification);
            l->setJustificationType(juce::Justification::centred);
            l->setFont(AmpLookAndFeel::font(10.5f, true, 0.05f));
            addAndMakeVisible(*l);
            labels_.push_back(std::move(l));
        }
        gain_ = std::make_unique<KnobWithLabel>(p.apvts, RackState::paramId(ids::tenGain, slot.instance).toRawUTF8(), "GAIN dB");
        volume_ = std::make_unique<KnobWithLabel>(p.apvts, RackState::paramId(ids::tenVolume, slot.instance).toRawUTF8(), "VOLUME dB");
        addAndMakeVisible(*gain_);
        addAndMakeVisible(*volume_);
    }

    int height() const override { return 190; }

    void resized() override {
        auto r = getLocalBounds().reduced(6, 2);
        auto knobs = r.removeFromRight(2 * 78);
        gain_->setBounds(knobs.removeFromLeft(78).withHeight(100));
        volume_->setBounds(knobs.removeFromLeft(78).withHeight(100));
        const int w = std::min(54, r.getWidth() / 10);
        for (int b = 0; b < 10; ++b) {
            auto cell = r.removeFromLeft(w);
            labels_[std::size_t(b)]->setBounds(cell.removeFromBottom(16));
            sliders_[std::size_t(b)]->setBounds(cell.reduced(w / 4, 2));
        }
    }

private:
    std::vector<std::unique_ptr<juce::Slider>> sliders_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> atts_;
    std::unique_ptr<KnobWithLabel> gain_, volume_;
};

// Amp and cabinet: their controls arrive with the unit that hosts them; until then a line says where they are.
class NoteBody final : public ModuleUnit::Body {
public:
    explicit NoteBody(const juce::String& text) {
        label_.setText(text, juce::dontSendNotification);
        label_.setFont(AmpLookAndFeel::font(12.0f));
        addAndMakeVisible(label_);
    }
    int height() const override { return 34; }
    void resized() override { label_.setBounds(getLocalBounds().reduced(10, 4)); }

private:
    juce::Label label_;
};

}  // namespace

juce::String ModuleUnit::title(sas::SlotRef slot) {
    // The second amp and cabinet are the ones of lane B.
    if (slot.instance > 0 && (slot.type == ModuleType::Amp || slot.type == ModuleType::Cabinet)) return tr(nameOf(slot.type)) + " B";
    return tr(nameOf(slot.type)) + (slot.instance > 0 ? " " + juce::String(slot.instance + 1) : juce::String());
}

juce::String ModuleUnit::dragDescription(sas::SlotRef slot) { return "rack:" + juce::String(int(slot.type)) + ":" + juce::String(slot.instance); }

bool ModuleUnit::parseDragDescription(const juce::String& text, sas::SlotRef& slot) {
    if (!text.startsWith("rack:")) return false;
    const auto parts = juce::StringArray::fromTokens(text.fromFirstOccurrenceOf("rack:", false, false), ":", "");
    if (parts.size() != 2) return false;
    const int type = parts[0].getIntValue(), instance = parts[1].getIntValue();
    if (type < 0 || type >= sas::kModuleTypeCount || instance < 0 || instance >= sas::Rack::kMaxInstances) return false;
    slot = {static_cast<ModuleType>(type), instance};
    return true;
}

ModuleUnit::ModuleUnit(SasProcessor& processor, sas::SlotRef slot, const HostedBodyFactory& hosted) : processor_(processor), slot_(slot) {
    if (slot.type == ModuleType::Amp || slot.type == ModuleType::Cabinet) {
        if (hosted) body_ = hosted(slot.type, slot.instance);
        if (!body_) body_ = std::make_unique<NoteBody>(tr("The controls of this module are not available here."));
    } else if (slot.type == ModuleType::Splitter) {
        body_ = std::make_unique<NoteBody>(tr("The signal splits here into lane A (above) and lane B (below), and joins again at the mixer."));
    } else if (slot.type == ModuleType::Eq) {
        body_ = std::make_unique<EqBody>(processor, slot);
    } else if (slot.type == ModuleType::TenBandEq) {
        body_ = std::make_unique<TenBandBody>(processor, slot);
    } else {
        body_ = std::make_unique<ParamBody>(processor, slot);
    }
    addAndMakeVisible(*body_);

    name_.setText(title(slot), juce::dontSendNotification);
    name_.setFont(AmpLookAndFeel::font(11.0f, true, 0.08f));
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);
    if (const char* on = RackState::onId(slot.type)) {
        on_.setButtonText(tr("On"));
        addAndMakeVisible(on_);
        onAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, RackState::paramId(on, slot.instance), on_);
    }
    for (auto* b : {&up_, &down_, &remove_, &add_}) addAndMakeVisible(*b);
    up_.setTooltip(tr("Move up (earlier in the signal path)"));
    down_.setTooltip(tr("Move down (later in the signal path)"));
    remove_.setTooltip(tr("Remove this module"));
    add_.setTooltip(tr("Add a module after this one"));
    up_.onClick = [this] { if (onMove) onMove(slot_, -1); };
    down_.onClick = [this] { if (onMove) onMove(slot_, +1); };
    remove_.onClick = [this] { if (onRemove) onRemove(slot_); };
    add_.onClick = [this] { if (onAddAfter) onAddAfter(slot_); };
    remove_.setEnabled(!RackState::isFixed(slot));
    add_.setTooltip(tr("Add a module after this one"));

    switch (slot.type) {
        case ModuleType::Amp:
            // The factory presets of whichever amp this one has selected now (the list is built when the button is pressed).
            extraB_.setButtonText(tr("Presets"));
            extraB_.onClick = [this] {
                const auto& amps = processor_.amps();
                const auto& presets = amps[std::size_t(processor_.selectedAmpIndex(slot_.instance))].presets;
                juce::PopupMenu menu;
                if (presets.empty()) menu.addItem(1, tr("This amp has no presets"), false);
                for (std::size_t i = 0; i < presets.size(); ++i) menu.addItem(int(i) + 1, tr(presets[i].name.c_str()));
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                    if (choice > 0) processor_.applyAmpPreset(choice - 1, slot_.instance);
                });
            };
            break;
        case ModuleType::Delay:
            extraA_.setButtonText(tr("Tap"));
            extraB_.setButtonText(tr("Presets"));
            extraA_.onClick = [this] {
                const double now = juce::Time::getMillisecondCounterHiRes();
                const double gap = now - lastTapMs_;
                lastTapMs_ = now;
                if (gap < 200.0 || gap > 1500.0) return;  // the first tap, or a pause: start over
                if (auto* sync = processor_.apvts.getParameter(RackState::paramId(ids::dlySync, slot_.instance))) sync->setValueNotifyingHost(0.0f);
                if (auto* time = processor_.apvts.getParameter(RackState::paramId(ids::dlyTime, slot_.instance))) {
                    time->beginChangeGesture();
                    time->setValueNotifyingHost(time->convertTo0to1(float(gap)));
                    time->endChangeGesture();
                }
            };
            extraB_.onClick = [this] {
                juce::PopupMenu menu;
                for (int i = 0; i < kDelayPresetCount; ++i) menu.addItem(i + 1, tr(kDelayPresets[i].name));
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                    if (choice > 0) processor_.applyDelayPreset(choice - 1, slot_.instance);
                });
            };
            break;
        case ModuleType::Reverb:
            extraB_.setButtonText(tr("Presets"));
            extraB_.onClick = [this] {
                juce::PopupMenu menu;
                for (int i = 0; i < kReverbPresetCount; ++i) menu.addItem(i + 1, tr(kReverbPresets[i].name));
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                    if (choice > 0) processor_.applyReverbPreset(choice - 1, slot_.instance);
                });
            };
            break;
        case ModuleType::TapeEcho:
            extraB_.setButtonText(tr("Presets"));
            extraB_.onClick = [this] {
                juce::PopupMenu menu;
                for (int i = 0; i < kTapePresetCount; ++i) menu.addItem(i + 1, tr(kTapePresets[i].name));
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                    if (choice > 0) processor_.applyTapePreset(choice - 1, slot_.instance);
                });
            };
            break;
        case ModuleType::Fuzz:
            extraB_.setButtonText(tr("Presets"));
            extraB_.onClick = [this] {
                juce::PopupMenu menu;
                for (int i = 0; i < kFuzzPresetCount; ++i) menu.addItem(i + 1, tr(kFuzzPresets[i].name));
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                    if (choice > 0) processor_.applyFuzzPreset(choice - 1, slot_.instance);
                });
            };
            break;
        case ModuleType::Wah:
            extraB_.setButtonText(tr("Presets"));
            extraB_.onClick = [this] {
                juce::PopupMenu menu;
                for (int i = 0; i < kWahPresetCount; ++i) menu.addItem(i + 1, tr(kWahPresets[i].name));
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                    if (choice > 0) processor_.applyWahPreset(choice - 1, slot_.instance);
                });
            };
            break;
        case ModuleType::Eq:
            extraB_.setButtonText(tr("Presets"));
            extraB_.onClick = [this] {
                juce::PopupMenu menu;
                for (int i = 0; i < kEqPresetCount; ++i) menu.addItem(i + 1, i == 0 ? juce::String(tr("Flat (reset)")) : tr(kEqPresets[i].name));
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                    if (choice > 0) processor_.applyEqPreset(choice - 1, slot_.instance);
                });
            };
            break;
        case ModuleType::NamPedal:
            extraA_.setButtonText(tr("Load..."));
            extraB_.setButtonText(tr("Clear"));
            extraA_.onClick = [this] {
                chooser_ = std::make_unique<juce::FileChooser>(tr("Choose a Neural Amp Modeler pedal model"), juce::File{}, "*.nam");
                chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
                    const auto file = fc.getResult();
                    if (file == juce::File{}) return;
                    if (processor_.loadNamPedal(file, slot_.instance)) {
                        if (auto* on = processor_.apvts.getParameter(RackState::paramId(ids::nmpOn, slot_.instance))) on->setValueNotifyingHost(1.0f);  // audible right away
                    } else {
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Schematic Amp Sim",
                                                               tr("Could not load that model: ") + i18n::namMessage(processor_.namPedalMessage(slot_.instance)) + ".");
                    }
                    refresh();
                });
            };
            extraB_.onClick = [this] {
                processor_.clearNamPedal(slot_.instance);
                refresh();
            };
            break;
        default:
            // The modules with a plain table of presets (plugin/ModulePresets.h): gate, compressor, overdrive, the modulation effects, pitch, boost, limiter.
            if (modulePresets(slot.type).count > 0) {
                extraB_.setButtonText(tr("Presets"));
                extraB_.onClick = [this] {
                    const auto list = modulePresets(slot_.type);
                    juce::PopupMenu menu;
                    for (int i = 0; i < list.count; ++i) menu.addItem(i + 1, tr(list.items[i].name));
                    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&extraB_), [this](int choice) {
                        if (choice > 0) processor_.applyModulePreset(slot_.type, choice - 1, slot_.instance);
                    });
                };
            }
            break;
    }
    for (auto* b : {&extraA_, &extraB_}) addChildComponent(*b);
    extraA_.setVisible(extraA_.getButtonText().isNotEmpty());
    extraB_.setVisible(extraB_.getButtonText().isNotEmpty());
    refresh();
    setSize(600, preferredHeight());
}

ModuleUnit::~ModuleUnit() = default;

int ModuleUnit::preferredHeight() const { return kHeaderHeight + body_->height() + 6; }

void ModuleUnit::refresh() {
    if (slot_.type != ModuleType::NamPedal) return;
    const bool loaded = processor_.namPedalLoaded(slot_.instance);
    const auto msg = i18n::namMessage(processor_.namPedalMessage(slot_.instance));
    name_.setText(title(slot_) + "   " + (loaded ? processor_.namPedalName(slot_.instance) + (msg.isEmpty() ? juce::String() : "  (" + msg + ")") : juce::String(tr("no pedal model"))),
                  juce::dontSendNotification);
}

void ModuleUnit::tick() {
    body_->tick();
    setCpu(processor_.rack().moduleCpu(slot_));
}

void ModuleUnit::setSumsToMono(bool sums) {
    if (sums == sumsToMono_) return;
    sumsToMono_ = sums;
    repaint();
}

void ModuleUnit::setHot(bool hot) {
    if (hot == hot_) return;
    hot_ = hot;
    repaint();
}

void ModuleUnit::setCpu(float percent) {
    if (std::abs(percent - cpu_) < 0.05f) return;
    cpu_ = percent;
    repaint();
}

void ModuleUnit::paint(juce::Graphics& g) {
    const auto* look = lookOf(*this);
    const auto panel = look ? look->panel() : juce::Colour(0xff2a2723);
    const auto accent = look ? look->accent() : juce::Colour(0xffd9a441);
    const auto edge = look ? look->panelEdge() : juce::Colour(0xff4a443c);
    const auto label = look ? look->label() : juce::Colours::grey;
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(panel.brighter(0.10f), 0.0f, 0.0f, panel.darker(0.15f), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle(b, 7.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(b.reduced(0.5f), 7.0f, 1.0f);
    // The handle: six dots, where a drag starts.
    g.setColour(label.withAlpha(0.7f));
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 2; ++c) g.fillEllipse(8.0f + float(c) * 6.0f, 8.0f + float(r) * 6.0f, 3.0f, 3.0f);
    g.setColour(accent);
    g.fillRoundedRectangle(24.0f, 11.0f, 3.0f, 9.0f, 1.5f);
    g.setFont(AmpLookAndFeel::font(10.0f));
    if (sumsToMono_) {
        g.setColour(label.withAlpha(0.8f));
        g.drawText(tr("sums to mono"), getWidth() - 400, 0, 100, kHeaderHeight, juce::Justification::centredRight);
    }
    if (cpu_ > 0.0f) {
        g.setColour(hot_ ? accent : label.withAlpha(0.8f));
        g.drawText(juce::String(cpu_, 1) + " % cpu", getWidth() - 300, 0, 70, kHeaderHeight, juce::Justification::centredRight);
    }
}

void ModuleUnit::resized() {
    auto r = getLocalBounds();
    auto head = r.removeFromTop(kHeaderHeight).reduced(4, 4);
    head.removeFromLeft(24);
    name_.setColour(juce::Label::textColourId, lookOf(*this) ? lookOf(*this)->label() : juce::Colours::grey);
    for (auto* b : {&add_, &remove_, &down_, &up_}) b->setBounds(head.removeFromRight(24)), head.removeFromRight(3);
    if (extraB_.isVisible()) extraB_.setBounds(head.removeFromRight(64)), head.removeFromRight(3);
    if (extraA_.isVisible()) extraA_.setBounds(head.removeFromRight(64)), head.removeFromRight(3);
    if (onAtt_) on_.setBounds(head.removeFromRight(56));
    name_.setBounds(head.withTrimmedRight(0));
    body_->setBounds(r.withTrimmedBottom(4));
}

void ModuleUnit::mouseDown(const juce::MouseEvent&) {}

void ModuleUnit::mouseDrag(const juce::MouseEvent& e) {
    // A drag that starts on the handle (the left edge of the header) moves the unit.
    if (e.getMouseDownPosition().x > 24 || e.getMouseDownPosition().y > kHeaderHeight || e.getDistanceFromDragStart() < 6) return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        if (!container->isDragAndDropActive()) container->startDragging(dragDescription(slot_), this);
}
