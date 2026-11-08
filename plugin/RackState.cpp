#include "RackState.h"

#include <algorithm>
#include <cmath>

#include "PluginProcessor.h"
#include "sas/eq.h"

namespace RackState {

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using sas::ModuleType;

namespace {

// Caps (docs/MODULAR_RACK.md, 4.1; the CPU table in section 6 holds them).
constexpr int kCaps[sas::kModuleTypeCount] = {2 /*Amp*/, 2 /*Cabinet*/, 2 /*Gate*/, 2 /*Compressor*/, 4 /*Overdrive*/, 2 /*NamPedal*/, 2 /*Phaser*/,
                                              2 /*Chorus*/, 2 /*Vibe*/, 2 /*Pitch*/, 2 /*Eq*/, 2 /*Delay*/, 2 /*Reverb*/, 1 /*Splitter*/, 1 /*Mixer*/, 2 /*Tremolo*/, 2 /*Flanger*/, 2 /*Wah*/, 2 /*Fuzz*/, 2 /*Boost*/, 2 /*Limiter*/, 2 /*TapeEcho*/, 2 /*Acoustic*/};

// Mono: both channels of a stereo pair get the same modulation. Stereo: the right channel's modulation is out of step with the left's.
std::unique_ptr<juce::AudioParameterChoice> channelMode(const juce::ParameterID& id, const juce::String& name) {
    return std::make_unique<juce::AudioParameterChoice>(id, name, juce::StringArray{"Mono", "Stereo"}, 0);
}

juce::NormalisableRange<float> logRange(float a, float b) {
    juce::NormalisableRange<float> r(a, b);
    r.setSkewForCentre(std::sqrt(a * b));
    return r;
}

std::unique_ptr<juce::AudioParameterFloat> hz(const juce::String& id, const juce::String& name, float lo, float hi, float def) {
    juce::NormalisableRange<float> r(lo, hi, 0.0f);
    r.setSkewForCentre(std::sqrt(lo * hi));
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, kParamVersion}, name, r, def,
                                                       juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); }));
}

std::unique_ptr<juce::AudioParameterFloat> pct(const juce::String& id, const juce::String& name, float lo, float hi, float def) {
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, kParamVersion}, name, juce::NormalisableRange<float>(lo, hi, 0.0f), def,
                                                       juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); }));
}

std::unique_ptr<juce::AudioParameterBool> toggle(const juce::String& id, const juce::String& name, bool def) {
    return std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, kParamVersion}, name, def);
}

std::unique_ptr<juce::AudioParameterFloat> rate(const juce::String& id, const juce::String& name, float lo, float hi, float centre, float def, const char* label) {
    juce::NormalisableRange<float> r(lo, hi, 0.0f);
    r.setSkewForCentre(centre);
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, kParamVersion}, name, r, def,
                                                       juce::AudioParameterFloatAttributes().withLabel(label).withStringFromValueFunction([](float v, int) { return juce::String(v, 2); }));
}

}  // namespace

int maxInstances(ModuleType type) { return kCaps[std::size_t(type)]; }

juce::String paramId(const char* baseId, int instance) { return instance <= 0 ? juce::String(baseId) : juce::String(baseId) + "__" + juce::String(instance + 1); }

juce::String paramName(const char* baseName, int instance) { return instance <= 0 ? juce::String(baseName) : juce::String(baseName) + " " + juce::String(instance + 1); }

std::unique_ptr<juce::AudioParameterFloat> knob(const juce::String& id, const juce::String& name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, kParamVersion}, name, juce::NormalisableRange<float>(0.0f, 10.0f, 0.0f), def,
                                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(v, 1); }));
}

std::unique_ptr<juce::AudioParameterFloat> dbParam(const juce::String& id, const juce::String& name, float defaultDb) {
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, kParamVersion}, name, juce::NormalisableRange<float>(-24.0f, 24.0f, 0.0f), defaultDb,
                                                       juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 1); }));
}

const std::vector<const char*>& paramIds(ModuleType type) {
    static const std::vector<const char*> none;
    static const std::vector<const char*> cabinet{ids::cabOn, ids::cab, ids::irBlend, ids::irWidth, ids::doubleAmount, ids::doubleTime};
    static const std::vector<const char*> amp{ids::amp, ids::volume, ids::volume2, ids::bass, ids::middle, ids::treble, ids::presence, ids::master, ids::power, ids::resonance,
                                              ids::vibSpeed, ids::vibIntensity, ids::vibOn, ids::aeq[0], ids::aeq[1], ids::aeq[2], ids::aeq[3], ids::aeq[4], ids::input, ids::quality, ids::jack, ids::sel[0], ids::sel[1], ids::sel[2], ids::sel[3], ids::sel[4], ids::sel[5],
                                              ids::pickup, ids::cable, ids::tube, ids::wear, ids::unit, ids::noise, ids::bias, ids::speaker, ids::speakerIntense};
    static const std::vector<const char*> mixer{ids::mixLevelA, ids::mixLevelB, ids::mixPanA, ids::mixPanB, ids::mixInvertB};
    static const std::vector<const char*> gate{ids::gateOn, ids::gateThreshold, ids::gateRelease};
    static const std::vector<const char*> comp{ids::compOn, ids::compSustain, ids::compBlend, ids::compTone, ids::compLevel, ids::compFast};
    static const std::vector<const char*> drive{ids::tsOn, ids::tsDrive, ids::tsTone, ids::tsLevel};
    static const std::vector<const char*> nam{ids::nmpOn, ids::nmpDrive, ids::nmpLevel};
    static const std::vector<const char*> phaser{ids::phOn, ids::phSpeed, ids::phIntense, ids::phMix, ids::phStereo};
    static const std::vector<const char*> chorus{ids::chOn, ids::chMode, ids::chRate, ids::chDepth, ids::chJuno, ids::chMix, ids::chStereo};
    static const std::vector<const char*> vibe{ids::vbOn, ids::vbSpeed, ids::vbIntensity, ids::vbVibrato, ids::vbMix, ids::vbStereo};
    static const std::vector<const char*> tremolo{ids::trOn, ids::trRate, ids::trDepth, ids::trShape, ids::trStereo};
    static const std::vector<const char*> flanger{ids::flOn, ids::flRate, ids::flDepth, ids::flManual, ids::flFeedback, ids::flMix, ids::flStereo};
    static const std::vector<const char*> wah{ids::wahOn, ids::wahMode, ids::wahPos, ids::wahHeel, ids::wahToe, ids::wahQ, ids::wahMix, ids::wahLevel, ids::wahSens,
                                              ids::wahAttack, ids::wahRelease, ids::wahDir, ids::wahRate, ids::wahStereo};
    static const std::vector<const char*> fuzz{ids::fzOn, ids::fzType, ids::fzFuzz, ids::fzTone, ids::fzBias, ids::fzMix, ids::fzLevel};
    static const std::vector<const char*> boost{ids::bstOn, ids::bstGain, ids::bstTone, ids::bstSoft};
    static const std::vector<const char*> acoustic{ids::acOn, ids::acType, ids::acBody, ids::acTop, ids::acAir, ids::acSquash, ids::acMix};
    static const std::vector<const char*> limiter{ids::limOn, ids::limGain, ids::limCeiling, ids::limRelease};
    static const std::vector<const char*> tape{ids::tpOn, ids::tpTime, ids::tpHeads, ids::tpIntensity, ids::tpMix, ids::tpTone, ids::tpWow, ids::tpFlutter, ids::tpSat, ids::tpStereo};
    static const std::vector<const char*> pitch{ids::pitch, ids::pitchFine};
    static const std::vector<const char*> eq = [] {
        std::vector<const char*> v{ids::eqOn, ids::eqHpf, ids::eqLpf};
        for (int b = 0; b < sas::EqParams::kBands; ++b) {
            v.push_back(ids::eqFreq[b]);
            v.push_back(ids::eqGain[b]);
            v.push_back(ids::eqQ[b]);
        }
        return v;
    }();
    static const std::vector<const char*> delay{ids::dlyOn, ids::dlyTime, ids::dlySync, ids::dlyFeedback, ids::dlyMix, ids::dlyTone, ids::dlyPing, ids::dlyAnalog, ids::dlyStereo};
    static const std::vector<const char*> reverb{ids::plOn, ids::plHall, ids::plDecay, ids::plPre, ids::plTone, ids::plLow, ids::plMix, ids::plMod, ids::plShimmer, ids::plShimmerInt, ids::plStereo};
    switch (type) {
        case ModuleType::Cabinet: return cabinet;
        case ModuleType::Amp: return amp;
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
        case ModuleType::Acoustic: return acoustic;
        case ModuleType::TapeEcho: return tape;
        case ModuleType::Limiter: return limiter;
        case ModuleType::Pitch: return pitch;
        case ModuleType::Eq: return eq;
        case ModuleType::Delay: return delay;
        case ModuleType::Reverb: return reverb;
        default: return none;
    }
}

const char* onId(ModuleType type) {
    switch (type) {
        case ModuleType::Cabinet: return ids::cabOn;
        case ModuleType::Gate: return ids::gateOn;
        case ModuleType::Compressor: return ids::compOn;
        case ModuleType::Overdrive: return ids::tsOn;
        case ModuleType::NamPedal: return ids::nmpOn;
        case ModuleType::Phaser: return ids::phOn;
        case ModuleType::Chorus: return ids::chOn;
        case ModuleType::Vibe: return ids::vbOn;
        case ModuleType::Tremolo: return ids::trOn;
        case ModuleType::Flanger: return ids::flOn;
        case ModuleType::Wah: return ids::wahOn;
        case ModuleType::Fuzz: return ids::fzOn;
        case ModuleType::Boost: return ids::bstOn;
        case ModuleType::Acoustic: return ids::acOn;
        case ModuleType::TapeEcho: return ids::tpOn;
        case ModuleType::Limiter: return ids::limOn;
        case ModuleType::Eq: return ids::eqOn;
        case ModuleType::Delay: return ids::dlyOn;
        case ModuleType::Reverb: return ids::plOn;
        default: return nullptr;
    }
}

void addParams(Layout& l, ModuleType type, int k) {
    auto I = [k](const char* base) { return paramId(base, k); };
    auto N = [k](const juce::String& base) { return paramName(base.toRawUTF8(), k); };
    auto id = [&](const juce::String& s) { return juce::ParameterID{s, kParamVersion}; };
    switch (type) {
        case ModuleType::Cabinet:
            l.add(toggle(I(ids::cabOn), N("Cabinet on"), true));
            if (k > 0) {  // the first cabinet's other parameters are declared with the amp's (they were there first)
                l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::cab)), N("Cabinet"), juce::StringArray{"Off", "Filter", "IR", "NAM"}, 1));
                l.add(knob(I(ids::irWidth), N("IR stereo width"), 0.0f));
                l.add(knob(I(ids::doubleAmount), N("Double tracking"), 0.0f));
                l.add(knob(I(ids::doubleTime), N("Double tracking time"), 5.0f));
                l.add(knob(I(ids::irBlend), N("IR blend"), 5.0f));
            }
            break;
        case ModuleType::Mixer: {
            auto db = [&](const char* base, const char* name) {
                return std::make_unique<juce::AudioParameterFloat>(id(I(base)), N(name), juce::NormalisableRange<float>(-24.0f, 12.0f, 0.0f), 0.0f,
                                                                    juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 1); }));
            };
            auto pan = [&](const char* base, const char* name) {
                return std::make_unique<juce::AudioParameterFloat>(id(I(base)), N(name), juce::NormalisableRange<float>(-100.0f, 100.0f, 0.0f), 0.0f,
                                                                    juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                                                                        return v < -0.5f ? "L" + juce::String(int(-v + 0.5f)) : v > 0.5f ? "R" + juce::String(int(v + 0.5f)) : juce::String("C");
                                                                    }));
            };
            l.add(db(ids::mixLevelA, "Mixer level A"));
            l.add(db(ids::mixLevelB, "Mixer level B"));
            l.add(pan(ids::mixPanA, "Mixer pan A"));
            l.add(pan(ids::mixPanB, "Mixer pan B"));
            l.add(toggle(I(ids::mixInvertB), N("Mixer phase B inverted"), false));
            break;
        }
        case ModuleType::Gate:
            l.add(toggle(I(ids::gateOn), N("Noise gate"), false));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::gateThreshold)), N("Gate threshold"), juce::NormalisableRange<float>(-80.0f, -20.0f, 0.0f), -60.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::gateRelease)), N("Gate release"), juce::NormalisableRange<float>(20.0f, 500.0f, 0.0f, 0.5f), 120.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
            break;
        case ModuleType::Compressor:
            l.add(toggle(I(ids::compOn), N("Compressor"), false));
            l.add(knob(I(ids::compSustain), N("Compressor sustain"), 5.0f));
            l.add(knob(I(ids::compBlend), N("Compressor blend"), 7.0f));
            l.add(knob(I(ids::compTone), N("Compressor tone"), 5.0f));
            l.add(knob(I(ids::compLevel), N("Compressor level"), 7.5f));
            l.add(toggle(I(ids::compFast), N("Compressor fast release"), false));
            break;
        case ModuleType::Overdrive:
            l.add(toggle(I(ids::tsOn), N("Tube Screamer"), false));
            l.add(knob(I(ids::tsDrive), N("TS Drive"), 5.0f));
            l.add(knob(I(ids::tsTone), N("TS Tone"), 5.0f));
            l.add(knob(I(ids::tsLevel), N("TS Level"), 5.0f));
            break;
        case ModuleType::NamPedal:
            l.add(toggle(I(ids::nmpOn), N("NAM pedal"), false));
            l.add(dbParam(I(ids::nmpDrive), N("NAM pedal drive")));
            l.add(dbParam(I(ids::nmpLevel), N("NAM pedal level")));
            break;
        case ModuleType::Phaser:
            l.add(toggle(I(ids::phOn), N("Phaser"), false));
            l.add(rate(I(ids::phSpeed), N("Phaser speed"), 0.1f, 8.0f, 1.0f, 0.8f, "Hz"));
            l.add(toggle(I(ids::phIntense), N("Phaser intense"), false));
            l.add(pct(I(ids::phMix), N("Phaser mix"), 0.0f, 100.0f, 100.0f));
            l.add(channelMode(id(I(ids::phStereo)), N("Phaser channels")));
            break;
        case ModuleType::Chorus:
            l.add(toggle(I(ids::chOn), N("Chorus"), false));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::chMode)), N("Chorus mode"), juce::StringArray{"CE-2", "Juno", "JC"}, 0));
            l.add(rate(I(ids::chRate), N("Chorus rate"), 0.1f, 8.0f, 1.4f, 1.2f, "Hz"));
            l.add(pct(I(ids::chDepth), N("Chorus depth"), 0.0f, 100.0f, 50.0f));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::chJuno)), N("Chorus Juno button"), juce::StringArray{"I", "II", "I+II"}, 0));
            l.add(pct(I(ids::chMix), N("Chorus mix"), 0.0f, 100.0f, 100.0f));
            l.add(channelMode(id(I(ids::chStereo)), N("Chorus channels")));
            break;
        case ModuleType::Vibe:
            l.add(toggle(I(ids::vbOn), N("Vibe"), false));
            l.add(rate(I(ids::vbSpeed), N("Vibe speed"), 0.3f, 10.0f, 3.0f, 3.0f, "Hz"));
            l.add(pct(I(ids::vbIntensity), N("Vibe intensity"), 0.0f, 100.0f, 70.0f));
            l.add(toggle(I(ids::vbVibrato), N("Vibe vibrato"), false));
            l.add(pct(I(ids::vbMix), N("Vibe mix"), 0.0f, 100.0f, 100.0f));
            l.add(channelMode(id(I(ids::vbStereo)), N("Vibe channels")));
            break;
        case ModuleType::Tremolo:
            l.add(toggle(I(ids::trOn), N("Tremolo"), false));
            l.add(rate(I(ids::trRate), N("Tremolo rate"), 0.5f, 15.0f, 4.0f, 4.0f, "Hz"));
            l.add(pct(I(ids::trDepth), N("Tremolo depth"), 0.0f, 100.0f, 50.0f));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::trShape)), N("Tremolo shape"), juce::StringArray{"Sine", "Triangle", "Square"}, 0));
            l.add(channelMode(id(I(ids::trStereo)), N("Tremolo channels")));
            break;
        case ModuleType::Flanger:
            l.add(toggle(I(ids::flOn), N("Flanger"), false));
            l.add(rate(I(ids::flRate), N("Flanger rate"), 0.05f, 5.0f, 0.5f, 0.4f, "Hz"));
            l.add(pct(I(ids::flDepth), N("Flanger depth"), 0.0f, 100.0f, 70.0f));
            l.add(pct(I(ids::flManual), N("Flanger manual"), 0.0f, 100.0f, 40.0f));
            l.add(pct(I(ids::flFeedback), N("Flanger feedback"), -90.0f, 90.0f, 50.0f));
            l.add(pct(I(ids::flMix), N("Flanger mix"), 0.0f, 100.0f, 100.0f));
            l.add(channelMode(id(I(ids::flStereo)), N("Flanger channels")));
            break;
        case ModuleType::TapeEcho: {
            juce::NormalisableRange<float> time(40.0f, 800.0f, 0.0f);
            time.setSkewForCentre(200.0f);
            l.add(toggle(I(ids::tpOn), N("Tape echo"), false));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::tpTime)), N("Tape time"), time, 180.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::tpHeads)), N("Tape heads"), juce::StringArray{"1", "2", "3", "1+2", "2+3", "1+3", "1+2+3"}, 3));
            l.add(pct(I(ids::tpIntensity), N("Tape intensity"), 0.0f, 110.0f, 45.0f));
            l.add(pct(I(ids::tpMix), N("Tape mix"), 0.0f, 100.0f, 30.0f));
            l.add(hz(I(ids::tpTone), N("Tape tone"), 1500.0f, 10000.0f, 5000.0f));
            l.add(pct(I(ids::tpWow), N("Tape wow"), 0.0f, 100.0f, 30.0f));
            l.add(pct(I(ids::tpFlutter), N("Tape flutter"), 0.0f, 100.0f, 30.0f));
            l.add(pct(I(ids::tpSat), N("Tape saturation"), 0.0f, 100.0f, 40.0f));
            l.add(channelMode(id(I(ids::tpStereo)), N("Tape channels")));
            break;
        }
        case ModuleType::Acoustic:
            l.add(toggle(I(ids::acOn), N("Acoustic"), false));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::acType)), N("Acoustic body type"), juce::StringArray{"Dreadnought", "Jumbo", "Parlor", "Piezo"}, 0));
            l.add(knob(I(ids::acBody), N("Acoustic body"), 6.0f));
            l.add(knob(I(ids::acTop), N("Acoustic top"), 5.0f));
            l.add(knob(I(ids::acAir), N("Acoustic air"), 4.0f));
            l.add(knob(I(ids::acSquash), N("Acoustic squash"), 4.0f));
            l.add(pct(I(ids::acMix), N("Acoustic mix"), 0.0f, 100.0f, 100.0f));
            break;
        case ModuleType::Boost:
            l.add(toggle(I(ids::bstOn), N("Boost"), false));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::bstGain)), N("Boost gain"), juce::NormalisableRange<float>(0.0f, 30.0f, 0.0f), 12.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));
            l.add(knob(I(ids::bstTone), N("Boost tone"), 5.0f));
            l.add(toggle(I(ids::bstSoft), N("Boost soft clip"), false));
            break;
        case ModuleType::Limiter:
            l.add(toggle(I(ids::limOn), N("Limiter"), false));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::limGain)), N("Limiter gain"), juce::NormalisableRange<float>(0.0f, 24.0f, 0.0f), 0.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::limCeiling)), N("Limiter ceiling"), juce::NormalisableRange<float>(-12.0f, 0.0f, 0.0f), -1.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));
            l.add(rate(I(ids::limRelease), N("Limiter release"), 10.0f, 1000.0f, 120.0f, 120.0f, "ms"));
            break;
        case ModuleType::Fuzz:
            l.add(toggle(I(ids::fzOn), N("Fuzz"), false));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::fzType)), N("Fuzz type"), juce::StringArray{"Face", "Muff", "Octave"}, 0));
            l.add(knob(I(ids::fzFuzz), N("Fuzz amount"), 6.0f));
            l.add(knob(I(ids::fzTone), N("Fuzz tone"), 5.0f));
            l.add(knob(I(ids::fzBias), N("Fuzz bias"), 7.0f));
            l.add(pct(I(ids::fzMix), N("Fuzz mix"), 0.0f, 100.0f, 100.0f));
            l.add(dbParam(I(ids::fzLevel), N("Fuzz level"), -6.0f));
            break;
        case ModuleType::Wah: {
            auto ms = [&](const char* base, const char* name, float lo, float hi, float centre, float def) {
                juce::NormalisableRange<float> r(lo, hi, 0.0f);
                r.setSkewForCentre(centre);
                return std::make_unique<juce::AudioParameterFloat>(id(I(base)), N(name), r, def,
                                                                   juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); }));
            };
            l.add(toggle(I(ids::wahOn), N("Wah"), false));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::wahMode)), N("Wah mode"), juce::StringArray{"Pedal", "Envelope", "LFO"}, 0));
            l.add(pct(I(ids::wahPos), N("Wah pedal"), 0.0f, 100.0f, 50.0f));
            l.add(hz(I(ids::wahHeel), N("Wah heel"), 200.0f, 1000.0f, 400.0f));
            l.add(hz(I(ids::wahToe), N("Wah toe"), 1000.0f, 4000.0f, 2000.0f));
            l.add(rate(I(ids::wahQ), N("Wah Q"), 0.7f, 12.0f, 4.0f, 4.0f, ""));
            l.add(pct(I(ids::wahMix), N("Wah mix"), 0.0f, 100.0f, 100.0f));
            l.add(dbParam(I(ids::wahLevel), N("Wah level"), 3.0f));
            l.add(pct(I(ids::wahSens), N("Wah sensitivity"), 0.0f, 100.0f, 50.0f));
            l.add(ms(ids::wahAttack, "Wah attack", 1.0f, 100.0f, 10.0f, 8.0f));
            l.add(ms(ids::wahRelease, "Wah release", 20.0f, 1000.0f, 150.0f, 120.0f));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::wahDir)), N("Wah direction"), juce::StringArray{"Up", "Down"}, 0));
            l.add(rate(I(ids::wahRate), N("Wah LFO rate"), 0.1f, 10.0f, 1.5f, 1.0f, "Hz"));
            l.add(channelMode(id(I(ids::wahStereo)), N("Wah channels")));
            break;
        }
        case ModuleType::Pitch:
            l.add(std::make_unique<juce::AudioParameterInt>(id(I(ids::pitch)), N("Pitch"), -12, 12, 0, juce::AudioParameterIntAttributes().withLabel("st")));
            l.add(std::make_unique<juce::AudioParameterInt>(id(I(ids::pitchFine)), N("Pitch fine"), -50, 50, 0, juce::AudioParameterIntAttributes().withLabel("cents")));
            break;
        case ModuleType::Eq: {
            // Frequencies and Q are on log scales; every gain starts at 0, so a new instance is flat.
            const sas::EqParams def;
            const float lo[8] = {20.0f, 60.0f, 200.0f, 1000.0f, 1000.0f, 100.0f, 800.0f, 3000.0f}, hi[8] = {500.0f, 1500.0f, 6000.0f, 12000.0f, 16000.0f, 2000.0f, 6000.0f, 16000.0f};
            l.add(toggle(I(ids::eqOn), N("EQ"), true));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::eqHpf)), N("EQ high-pass"), logRange(20.0f, 500.0f), 20.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v, int) { return v <= 20.5f ? juce::String("off") : juce::String(v, 0); })));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::eqLpf)), N("EQ low-pass"), logRange(2000.0f, 20000.0f), 20000.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v, int) { return v >= 19500.0f ? juce::String("off") : juce::String(v, 0); })));
            for (int b = 0; b < sas::EqParams::kBands; ++b) {
                const juce::String n(b + 1);
                l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::eqFreq[b])), N("EQ " + n + " frequency"), logRange(lo[b], hi[b]), float(def.band[std::size_t(b)].freq),
                                                                  juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
                l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::eqGain[b])), N("EQ " + n + " gain"), juce::NormalisableRange<float>(-15.0f, 15.0f, 0.1f), 0.0f,
                                                                  juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));
                l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::eqQ[b])), N("EQ " + n + " Q"), logRange(0.3f, 5.0f), float(def.band[std::size_t(b)].q),
                                                                  juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(v, 2); })));
            }
            break;
        }
        case ModuleType::Delay: {
            l.add(toggle(I(ids::dlyOn), N("Delay"), false));
            juce::NormalisableRange<float> time(20.0f, 1500.0f, 0.0f);
            time.setSkewForCentre(350.0f);
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::dlyTime)), N("Delay time"), time, 380.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::dlySync)), N("Delay sync"), juce::StringArray{"Free", "1/4", "1/8 dotted", "1/8", "1/16"}, 0));
            l.add(pct(I(ids::dlyFeedback), N("Delay feedback"), 0.0f, 92.0f, 35.0f));
            l.add(pct(I(ids::dlyMix), N("Delay mix"), 0.0f, 100.0f, 25.0f));
            l.add(hz(I(ids::dlyTone), N("Delay tone"), 1000.0f, 12000.0f, 6500.0f));
            l.add(toggle(I(ids::dlyPing), N("Delay ping-pong"), false));
            l.add(toggle(I(ids::dlyAnalog), N("Delay analog"), false));
            l.add(channelMode(id(I(ids::dlyStereo)), N("Delay channels")));
            break;
        }
        case ModuleType::Reverb: {
            l.add(toggle(I(ids::plOn), N("Reverb"), false));
            l.add(toggle(I(ids::plHall), N("Reverb hall"), false));
            juce::NormalisableRange<float> decay(0.3f, 8.0f, 0.0f);
            decay.setSkewForCentre(2.0f);
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::plDecay)), N("Reverb decay"), decay, 2.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("s").withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));
            l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::plPre)), N("Reverb pre-delay"), juce::NormalisableRange<float>(0.0f, 150.0f, 0.0f), 15.0f,
                                                              juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
            l.add(hz(I(ids::plTone), N("Reverb tone"), 2000.0f, 14000.0f, 8000.0f));
            l.add(hz(I(ids::plLow), N("Reverb low cut"), 20.0f, 800.0f, 150.0f));
            l.add(pct(I(ids::plMix), N("Reverb mix"), 0.0f, 100.0f, 22.0f));
            l.add(pct(I(ids::plMod), N("Reverb modulation"), 0.0f, 100.0f, 30.0f));
            l.add(pct(I(ids::plShimmer), N("Reverb shimmer"), 0.0f, 100.0f, 0.0f));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::plShimmerInt)), N("Reverb shimmer interval"), juce::StringArray{"Octave", "Octave + 5th"}, 0));
            l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::plStereo)), N("Reverb channels"), juce::StringArray{"Mono", "Stereo"}, 1));  // stereo, as it always was
            break;
        }
        default: break;
    }
}

// The parameters of the second amp: the same as the first one's, with the same ranges and defaults (the first ones are declared in
// SasProcessor::createLayout, in the order the plug-in always had).
static void addAmpInstance(Layout& l, int k, const juce::StringArray& ampNames) {
    auto I = [k](const char* base) { return paramId(base, k); };
    auto N = [k](const juce::String& base) { return paramName(base.toRawUTF8(), k); };
    auto id = [&](const juce::String& s) { return juce::ParameterID{s, kParamVersion}; };
    l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::amp)), N("Amp"), ampNames, 0));
    l.add(knob(I(ids::volume), N("Preamp Volume"), 6.0f));
    l.add(knob(I(ids::volume2), N("Preamp Volume II"), 5.0f));
    for (int e = 0; e < 5; ++e) l.add(knob(I(ids::aeq[e]), N("Graphic EQ " + juce::String(e + 1)), 5.0f));
    l.add(knob(I(ids::bass), N("Bass"), 5.0f));
    l.add(knob(I(ids::middle), N("Middle"), 5.0f));
    l.add(knob(I(ids::treble), N("Treble"), 6.0f));
    l.add(knob(I(ids::presence), N("Presence"), 5.0f));
    l.add(knob(I(ids::master), N("Master"), 5.0f));
    l.add(knob(I(ids::power), N("Power"), 5.0f));
    l.add(knob(I(ids::vibSpeed), N("Vibrato Speed"), 5.0f));
    l.add(knob(I(ids::vibIntensity), N("Vibrato Intensity"), 5.0f));
    l.add(knob(I(ids::reverb), N("Reverb"), 3.0f));
    l.add(knob(I(ids::resonance), N("Resonance"), 5.0f));
    l.add(toggle(I(ids::vibOn), N("Vibrato"), false));
    l.add(dbParam(I(ids::input), N("Input")));
    l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::quality)), N("Quality"), juce::StringArray{"Normal (2x)", "High (4x)", "Lite (2x)", "Eco (2x)", "Efficient (2x/1x)"}, 4));
    l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::jack)), N("Input jack"),
                                                       juce::StringArray{"Jack 1", "Jack 2", "Jack 3", "Jack 4", "Jack 5", "Jack 6", "Jack 7", "Jack 8"}, 0));
    for (int i = 0; i < 6; ++i)
        l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::sel[i])), N("Amp switch " + juce::String(i + 1)),
                                                           juce::StringArray{"Position 1", "Position 2", "Position 3", "Position 4", "Position 5"}, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::pickup)), N("Guitar pickup"), juce::StringArray{"Off", "Single coil", "Humbucker", "P-90"}, 0));
    l.add(std::make_unique<juce::AudioParameterFloat>(id(I(ids::cable)), N("Guitar cable"), juce::NormalisableRange<float>(0.0f, 14.0f, 2.0f), 4.0f,
                                                       juce::AudioParameterFloatAttributes().withLabel("m").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
    l.add(knob(I(ids::speaker), N("Speaker dynamics"), 0.0f));
    l.add(toggle(I(ids::speakerIntense), N("Speaker dynamics intense"), false));
    l.add(knob(I(ids::bias), N("Bias"), 5.0f));
    l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::tube)), N("Power tubes"), juce::StringArray{ids::tubeTypes[0], ids::tubeTypes[1], ids::tubeTypes[2], ids::tubeTypes[3]}, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::noise)), N("Noise and hum"), juce::StringArray{"Off", "Low", "Real"}, 0));
    l.add(std::make_unique<juce::AudioParameterInt>(id(I(ids::wear)), N("Wear"), 0, 10, 0,
                                                     juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return juce::String(v * 10) + " %"; })));
    l.add(std::make_unique<juce::AudioParameterChoice>(id(I(ids::unit)), N("Unit"),
                                                       juce::StringArray{"Ideal", "Unit 1", "Unit 2", "Unit 3", "Unit 4", "Unit 5", "Unit 6", "Unit 7", "Unit 8", "Unit 9"}, 0));
}

void addExtraInstances(Layout& l, const juce::StringArray& ampNames) {
    for (int k = 1; k < maxInstances(ModuleType::Amp); ++k) addAmpInstance(l, k, ampNames);
    for (int t = 0; t < sas::kModuleTypeCount; ++t) {
        if (static_cast<ModuleType>(t) == ModuleType::Amp) continue;
        for (int k = 1; k < maxInstances(static_cast<ModuleType>(t)); ++k) addParams(l, static_cast<ModuleType>(t), k);
    }
}

bool isFixed(sas::SlotRef slot) {
    // Only the first gate stays. The amp and the cabinet can be removed (and added again); the second amp and cabinet of a parallel are ordinary modules
    // of lane B; the splitter and the mixer are not removed one by one: they go with the parallel.
    return slot.instance == 0 && slot.type == ModuleType::Gate;
}

sas::RackOrder defaultOrder() {
    sas::RackOrder o;
    o.slots[0] = {ModuleType::Gate, 0};
    o.slots[1] = {ModuleType::Amp, 0};
    o.slots[2] = {ModuleType::Cabinet, 0};
    o.count = 3;
    return o;
}

namespace {

bool modified(const juce::AudioProcessorValueTreeState& apvts, ModuleType type) {
    for (const char* id : paramIds(type))
        if (auto* p = apvts.getParameter(id))
            if (std::abs(p->getValue() - p->getDefaultValue()) > 1e-6f) return true;
    return false;
}

bool flag(const juce::AudioProcessorValueTreeState& apvts, const char* id) {
    auto* v = apvts.getRawParameterValue(id);
    return v && v->load() > 0.5f;
}

}  // namespace

sas::RackOrder fromLegacy(const juce::AudioProcessorValueTreeState& apvts, bool namPedalLoaded, bool everyModule) {
    sas::RackOrder o;
    auto add = [&](ModuleType t) { o.slots[std::size_t(o.count++)] = {t, 0}; };
    auto addIf = [&](ModuleType t) {
        if (everyModule || modified(apvts, t) || (t == ModuleType::NamPedal && namPedalLoaded)) add(t);
    };
    // The order the fixed-order plug-in ran in: pitch, gate, the modulation set "pre amp", the NAM pedal, the chain's compressor and overdrive,
    // the amp and the cabinet, the equaliser, the modulation set "post amp", delay and reverb.
    addIf(ModuleType::Pitch);
    add(ModuleType::Gate);  // the gate, the amp and the cabinet are always there
    const std::pair<ModuleType, const char*> mods[] = {{ModuleType::Phaser, ids::phPost}, {ModuleType::Chorus, ids::chPost}, {ModuleType::Vibe, ids::vbPost}};
    for (const auto& [t, post] : mods)
        if (!flag(apvts, post)) addIf(t);
    addIf(ModuleType::NamPedal);
    addIf(ModuleType::Compressor);
    addIf(ModuleType::Overdrive);
    add(ModuleType::Amp);
    add(ModuleType::Cabinet);
    addIf(ModuleType::Eq);
    for (const auto& [t, post] : mods)
        if (flag(apvts, post)) addIf(t);
    addIf(ModuleType::Delay);
    addIf(ModuleType::Reverb);
    return o;
}

juce::ValueTree toTree(const sas::RackOrder& order) {
    juce::ValueTree rack("RACK");
    for (int i = 0; i < order.count; ++i) {
        juce::ValueTree m("M");
        m.setProperty("type", sas::moduleTypeName(order.slots[std::size_t(i)].type), nullptr);
        m.setProperty("inst", order.slots[std::size_t(i)].instance, nullptr);
        if (order.lane[std::size_t(i)] != 0) m.setProperty("lane", int(order.lane[std::size_t(i)]), nullptr);
        rack.appendChild(m, nullptr);
    }
    return rack;
}

sas::RackOrder fromTree(const juce::ValueTree& rack) {
    sas::RackOrder o;
    bool seen[sas::kModuleTypeCount][sas::Rack::kMaxInstances] = {};
    int modules = 0;
    bool amp = false;
    for (int i = 0; i < rack.getNumChildren() && o.count < sas::RackOrder::kMax; ++i) {
        const auto m = rack.getChild(i);
        const auto type = sas::moduleTypeFromName(m["type"].toString().toStdString());
        if (!type) continue;  // a module this version does not know
        const int inst = int(m["inst"]);
        if (inst < 0 || inst >= maxInstances(*type) || seen[int(*type)][inst]) continue;
        if (*type == ModuleType::Amp && inst == 0) {
            if (amp) continue;
            amp = true;
        } else if (*type != ModuleType::Amp) {
            if (modules >= kMaxModules) continue;
            ++modules;
        }
        seen[int(*type)][inst] = true;
        o.lane[std::size_t(o.count)] = std::uint8_t(int(m["lane"]) == 1 ? 1 : 0);
        o.slots[std::size_t(o.count++)] = {*type, inst};
    }
    auto indexOf = [&](sas::SlotRef want) {
        for (int i = 0; i < o.count; ++i)
            if (o.slots[std::size_t(i)] == want) return i;
        return -1;
    };
    auto eraseAt = [&](int at) {
        for (int k = at; k + 1 < o.count; ++k) {
            o.slots[std::size_t(k)] = o.slots[std::size_t(k + 1)];
            o.lane[std::size_t(k)] = o.lane[std::size_t(k + 1)];
        }
        --o.count;
    };
    // A parallel is valid when it has a splitter and a mixer in order, and the second amp and cabinet (if there are any) are in lane B; anything else is
    // not a parallel at all and loses what belongs to one (the splitter, the mixer, lane B and the second amp and cabinet).
    bool parallel = o.valid() && o.splitterIndex() >= 0;
    if (parallel)
        for (const auto second : {sas::SlotRef{ModuleType::Amp, 1}, sas::SlotRef{ModuleType::Cabinet, 1}})
            if (const int i = indexOf(second); i >= 0 && o.lane[std::size_t(i)] != 1) parallel = false;
    if (!parallel)
        for (int i = o.count - 1; i >= 0; --i) {
            const auto slot = o.slots[std::size_t(i)];
            if (slot.type == ModuleType::Splitter || slot.type == ModuleType::Mixer || o.lane[std::size_t(i)] != 0 || (slot.type == ModuleType::Amp && slot.instance > 0) ||
                (slot.type == ModuleType::Cabinet && slot.instance > 0))
                eraseAt(i);
        }
    // The first gate is always there, first. (A rack saved without an amp or a cabinet loads without them: the player removed them.)
    auto insertSlot = [&](int at, sas::SlotRef slot) {
        if (o.count >= sas::RackOrder::kMax) {  // full: the last module that is not fixed makes room
            for (int i = o.count - 1; i >= 0; --i)
                if (!isFixed(o.slots[std::size_t(i)]) && o.slots[std::size_t(i)].type != ModuleType::Splitter && o.slots[std::size_t(i)].type != ModuleType::Mixer) {
                    eraseAt(i);
                    if (at > i) --at;
                    break;
                }
        }
        for (int i = o.count; i > at; --i) {
            o.slots[std::size_t(i)] = o.slots[std::size_t(i - 1)];
            o.lane[std::size_t(i)] = o.lane[std::size_t(i - 1)];
        }
        o.slots[std::size_t(at)] = slot;
        o.lane[std::size_t(at)] = 0;
        ++o.count;
    };
    if (indexOf({ModuleType::Gate, 0}) < 0) insertSlot(0, {ModuleType::Gate, 0});
    return o;
}

void resetParams(juce::AudioProcessorValueTreeState& apvts, sas::SlotRef slot) {
    for (const char* base : paramIds(slot.type))
        if (auto* p = apvts.getParameter(paramId(base, slot.instance))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->getDefaultValue());
            p->endChangeGesture();
        }
}

}  // namespace RackState
