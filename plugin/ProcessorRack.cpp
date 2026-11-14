#include "ProcessorCommon.h"

// ------------------------------------------------------------------ the rack

std::unique_ptr<sas::Module> SasProcessor::makeModule(sas::ModuleType type, int instance) {
    using T = sas::ModuleType;
    switch (type) {
        case T::Amp: {
            auto amp = std::make_unique<sas::AmpModule>();
            amp->setChain(amps_[instance].chain.get());
            return amp;
        }
        case T::Cabinet: return std::make_unique<CabinetModule>(*this, instance);
        case T::Gate: return std::make_unique<sas::GateModule>();
        case T::Compressor: return std::make_unique<sas::CompressorModule>();
        case T::Overdrive: return std::make_unique<sas::OverdriveModule>();
        case T::NamPedal: return std::make_unique<sas::NamPedalModule>();
        case T::Phaser: return std::make_unique<sas::PhaserModule>();
        case T::Chorus: return std::make_unique<sas::ChorusModule>();
        case T::Vibe: return std::make_unique<sas::VibeModule>();
        case T::Tremolo: return std::make_unique<sas::TremoloModule>();
        case T::Flanger: return std::make_unique<sas::FlangerModule>();
        case T::Wah: return std::make_unique<sas::WahModule>();
        case T::Fuzz: return std::make_unique<sas::FuzzModule>();
        case T::Boost: return std::make_unique<sas::BoostModule>();
        case T::TenBandEq: return std::make_unique<sas::TenBandEqModule>();
        case T::Octaver: return std::make_unique<sas::OctaverModule>();
        case T::Harmonist: return std::make_unique<sas::HarmonistModule>();
        case T::Acoustic: return std::make_unique<sas::AcousticModule>();
        case T::Klon: return std::make_unique<sas::KlonModule>();
        case T::Leslie: return std::make_unique<sas::LeslieModule>();
        case T::SpringReverb: return std::make_unique<sas::SpringReverbModule>();
        case T::TapeEcho: return std::make_unique<sas::TapeEchoModule>();
        case T::BbdDelay: return std::make_unique<sas::BbdDelayModule>();
        case T::Bitcrusher: return std::make_unique<sas::BitcrusherModule>();
        case T::Layers: return std::make_unique<sas::LayersModule>();
        case T::Distortion: return std::make_unique<sas::DistortionModule>();
        case T::Limiter: return std::make_unique<sas::LimiterModule>();
        case T::Pitch: return std::make_unique<sas::PitchModule>();
        case T::Eq: return std::make_unique<sas::EqModule>();
        case T::Delay: return std::make_unique<sas::DelayModule>();
        case T::Reverb: return std::make_unique<sas::ReverbModule>();
        case T::Splitter: return std::make_unique<sas::SplitterModule>();
        case T::Mixer: return std::make_unique<sas::MixerModule>();
    }
    return nullptr;
}

// Makes the rack match `rackOrder_`: builds the modules that are new (here, on the message thread), publishes the order, retires the ones that are gone.
void SasProcessor::syncRack(sas::Transition how) {
    auto inOrder = [this](sas::SlotRef s) {
        for (int i = 0; i < rackOrder_.count; ++i)
            if (rackOrder_.slots[std::size_t(i)] == s) return true;
        return false;
    };
    for (int t = 0; t < sas::kModuleTypeCount; ++t)
        for (int k = 0; k < RackState::maxInstances(static_cast<sas::ModuleType>(t)); ++k) {
            const sas::SlotRef s{static_cast<sas::ModuleType>(t), k};
            if (inOrder(s) && !rack_.module(s)) rack_.setModule(s, makeModule(s.type, s.instance));
        }
    rack_.publish(rackOrder_, how);
    for (int t = 0; t < sas::kModuleTypeCount; ++t)
        for (int k = 0; k < RackState::maxInstances(static_cast<sas::ModuleType>(t)); ++k) {
            const sas::SlotRef s{static_cast<sas::ModuleType>(t), k};
            if (!inOrder(s) && rack_.module(s)) rack_.removeModule(s);
        }
    rack_.collect();
}

void SasProcessor::setRackOrder(const sas::RackOrder& order) {
    rackOrder_ = RackState::fromTree(RackState::toTree(order));
    syncRack(sas::Transition::Immediate);
}

bool SasProcessor::canAdd(sas::ModuleType type, int lane) const {
    int modules = 0, instances = 0;
    for (int i = 0; i < rackOrder_.count; ++i) {
        if (rackOrder_.slots[std::size_t(i)].type != sas::ModuleType::Amp) ++modules;
        if (rackOrder_.slots[std::size_t(i)].type == type) ++instances;
    }
    // The two ends of a parallel come in with the parallel itself. An amp or a cabinet can be there twice: the first one and a second one, in the same line
    // (in series) or, with `lane` 1, in lane B of the parallel.
    const bool ends = type == sas::ModuleType::Splitter || type == sas::ModuleType::Mixer;
    const bool pair = type == sas::ModuleType::Amp || type == sas::ModuleType::Cabinet;
    if (ends) return false;
    if (pair) {
        if (lane == 1) {
            if (!hasParallel() || indexInOrder({type, 1}) >= 0) return false;
        } else if (indexInOrder({type, 0}) >= 0 && indexInOrder({type, 1}) >= 0) {
            return false;
        }
        return modules < RackState::kMaxModules || type == sas::ModuleType::Amp;
    }
    return modules < RackState::kMaxModules && instances < RackState::maxInstances(type);
}

// The entries of the order move together with their lane.
void SasProcessor::insertEntry(int at, sas::SlotRef slot, int lane) {
    for (int i = rackOrder_.count; i > at; --i) {
        rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i - 1)];
        rackOrder_.lane[std::size_t(i)] = rackOrder_.lane[std::size_t(i - 1)];
    }
    rackOrder_.slots[std::size_t(at)] = slot;
    rackOrder_.lane[std::size_t(at)] = std::uint8_t(lane);
    ++rackOrder_.count;
}

void SasProcessor::eraseEntry(int at) {
    for (int i = at; i + 1 < rackOrder_.count; ++i) {
        rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i + 1)];
        rackOrder_.lane[std::size_t(i)] = rackOrder_.lane[std::size_t(i + 1)];
    }
    --rackOrder_.count;
}

int SasProcessor::indexInOrder(sas::SlotRef slot) const {
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)] == slot) return i;
    return -1;
}

void SasProcessor::normalizeLanes() {
    const int s = rackOrder_.splitterIndex(), m = rackOrder_.mixerIndex();
    for (int i = 0; i < rackOrder_.count; ++i)
        if (s < 0 || m < 0 || i <= s || i >= m) rackOrder_.lane[std::size_t(i)] = 0;
}

bool SasProcessor::layoutOk() const {
    if (!rackOrder_.valid()) return false;
    return true;  // the amps and cabinets can be anywhere: in series, or in either lane of a parallel
}

// A module that is put in the rack plays at once: its switch is on (the parameter's own default is "off" for the pedals and effects).
static void turnOn(juce::AudioProcessorValueTreeState& apvts, sas::SlotRef slot) {
    if (const char* on = RackState::onId(slot.type))
        if (auto* p = apvts.getParameter(RackState::paramId(on, slot.instance))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(1.0f);
            p->endChangeGesture();
        }
}

bool SasProcessor::addModule(sas::ModuleType type, int insertAt, int wantedLane) {
    if (!canAdd(type, wantedLane)) return false;
    const bool pair = type == sas::ModuleType::Amp || type == sas::ModuleType::Cabinet;
    int instance = 0;
    if (pair) {
        instance = (wantedLane == 1 || indexInOrder({type, 0}) >= 0) ? 1 : 0;  // the first amp or cabinet, or the second one (lane B, or in series)
    } else {
        for (; instance < RackState::maxInstances(type); ++instance)
            if (indexInOrder({type, instance}) < 0) break;
    }
    const sas::SlotRef slot{type, instance};
    const int ampAt = std::max(0, indexInOrder({sas::ModuleType::Amp, 0}));
    if (insertAt < 0) {
        using T = sas::ModuleType;
        // Where the time effects start (the delay, the tape echo, the reverb and the limiter): a first amp or cabinet that is put back goes in front of them.
        auto firstTimeEffect = [&] {
            for (int i = 0; i < rackOrder_.count; ++i) {
                const auto t = rackOrder_.slots[std::size_t(i)].type;
                if (t == T::Delay || t == T::TapeEcho || t == T::BbdDelay || t == T::Reverb || t == T::SpringReverb || t == T::Limiter) return i;
            }
            return rackOrder_.count;
        };
        switch (type) {
            case T::Overdrive: insertAt = ampAt; break;  // next to the amp, behind a compressor
            case T::Compressor:  // in front of the overdrives that sit next to the amp, so the amp can run both as it always did
                insertAt = ampAt;
                while (insertAt > 0 && rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Overdrive) --insertAt;
                break;
            case T::Gate: case T::NamPedal: case T::Pitch: case T::Wah: case T::Fuzz: case T::Boost: case T::Acoustic: case T::Klon: case T::TenBandEq: case T::Octaver: case T::Harmonist: case T::Distortion: {
                // In front of the compressor and the overdrive that sit next to the amp, so the amp keeps running them as it always did.
                insertAt = ampAt;
                while (insertAt > 0 && (rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Overdrive || rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Compressor)) --insertAt;
                break;
            }
            case T::Amp: {
                if (instance == 0) {  // the first amp (put back after it was removed): in front of the cabinet, or before the time effects
                    const int cab = indexInOrder({T::Cabinet, 0});
                    insertAt = cab >= 0 ? cab : firstTimeEffect();
                    break;
                }
                if (wantedLane != 1) {  // the second amp in the same line: next to the first one, in front of the cabinet
                    const int cab = indexInOrder({T::Cabinet, 0}), amp = indexInOrder({T::Amp, 0});
                    insertAt = cab >= 0 && cab > amp ? cab : amp + 1;
                    break;
                }
                // the second amp of lane B: at the end of it, in front of its cabinet if it has one
                const int cabB = indexInOrder({T::Cabinet, 1});
                insertAt = cabB >= 0 ? cabB : rackOrder_.mixerIndex();
                break;
            }
            case T::Cabinet: {
                if (instance == 0) {  // the first cabinet: behind the amp, or before the time effects
                    const int amp = indexInOrder({T::Amp, 0});
                    insertAt = amp >= 0 ? amp + 1 : firstTimeEffect();
                    break;
                }
                // the second cabinet: behind the second amp, or at the end of lane B (in series: behind the first cabinet)
                const int ampB = indexInOrder({T::Amp, 1});
                if (wantedLane != 1) {
                    const int cab = indexInOrder({T::Cabinet, 0});
                    insertAt = ampB >= 0 ? ampB + 1 : cab >= 0 ? cab + 1 : firstTimeEffect();
                    break;
                }
                insertAt = ampB >= 0 ? ampB + 1 : rackOrder_.mixerIndex();
                break;
            }
            default: insertAt = rackOrder_.count; break;
        }
    }
    insertAt = juce::jlimit(0, rackOrder_.count, insertAt);
    // Inside the parallel a new module joins the lane of the one before it.
    const int s = rackOrder_.splitterIndex(), m = rackOrder_.mixerIndex();
    int lane = (s >= 0 && insertAt > s + 1 && insertAt <= m) ? rackOrder_.lane[std::size_t(insertAt - 1)] : 0;
    if (wantedLane == 1 && !hasParallel()) return false;
    if (pair && instance == 0) lane = 0;  // the first amp and cabinet are in the shared part or lane A
    if (wantedLane == 1) {  // into lane B, inside the parallel
        lane = 1;
        insertAt = juce::jlimit(s + 1, m, insertAt);
    }
    const auto saved = rackOrder_;
    insertEntry(insertAt, slot, lane);
    normalizeLanes();
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    RackState::resetParams(apvts, slot);
    turnOn(apvts, slot);
    if (slot == sas::SlotRef{sas::ModuleType::Amp, 1}) {
        // The second amp starts as a copy of the first one's choice, so the sum is at once a doubled amp.
        if (auto* p = apvts.getParameter(RackState::paramId(ids::amp, 1))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(apvts.getParameter(ids::amp)->getValue());
            p->endChangeGesture();
        }
    }
    if (slot == sas::SlotRef{sas::ModuleType::Cabinet, 1}) {  // and its cabinet with the default impulse
        clearImpulseResponse(0, 1);
        clearImpulseResponse(1, 1);
        loadBuiltInImpulseResponse(firstCab_, 0, 1);
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::removeModule(sas::SlotRef slot) {
    if (slot.type == sas::ModuleType::Splitter || slot.type == sas::ModuleType::Mixer) return removeParallel();  // the two ends go together
    if (RackState::isFixed(slot)) return false;  // the first gate is always there
    const int at = indexInOrder(slot);
    if (at < 0) return false;
    eraseEntry(at);
    syncRack(sas::Transition::Duck);
    if (slot.type == sas::ModuleType::NamPedal) clearNamPedal(slot.instance);  // its model goes with it, so it is not kept in the project
    if (slot == sas::SlotRef{sas::ModuleType::Cabinet, 1}) {                     // and so do the second cabinet's impulses
        clearImpulseResponse(0, 1);
        clearImpulseResponse(1, 1);
    }
    return true;
}

bool SasProcessor::canReplace(sas::SlotRef slot, sas::ModuleType type) const {
    using T = sas::ModuleType;
    // The amps, the cabinets, the first gate and the two ends of a parallel stay; and nothing is replaced by one of those, or by its own type.
    for (const auto t : {slot.type, type})
        if (t == T::Amp || t == T::Cabinet || t == T::Splitter || t == T::Mixer) return false;
    if (RackState::isFixed(slot) || type == slot.type || indexInOrder(slot) < 0) return false;
    int instances = 0;
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)].type == type) ++instances;
    return instances < RackState::maxInstances(type);
}

bool SasProcessor::replaceModule(sas::SlotRef slot, sas::ModuleType type) {
    if (!canReplace(slot, type)) return false;
    int instance = 0;
    for (; instance < RackState::maxInstances(type); ++instance)
        if (indexInOrder({type, instance}) < 0) break;
    const sas::SlotRef fresh{type, instance};
    const int at = indexInOrder(slot);
    const auto saved = rackOrder_;
    rackOrder_.slots[std::size_t(at)] = fresh;  // same place, same lane
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    RackState::resetParams(apvts, fresh);
    turnOn(apvts, fresh);
    syncRack(sas::Transition::Duck);
    if (slot.type == sas::ModuleType::NamPedal) clearNamPedal(slot.instance);  // the old one's model goes with it, as when it is removed
    return true;
}

bool SasProcessor::moveModule(sas::SlotRef slot, int newIndex) {
    if (slot.type == sas::ModuleType::Splitter || slot.type == sas::ModuleType::Mixer) return false;  // the ends of the parallel stay where they are
    const int at = indexInOrder(slot);
    if (at < 0) return false;
    const auto saved = rackOrder_;
    const int lane = rackOrder_.lane[std::size_t(at)];
    eraseEntry(at);
    newIndex = juce::jlimit(0, rackOrder_.count, newIndex);
    insertEntry(newIndex, slot, lane);
    normalizeLanes();
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::moveModuleToLane(sas::SlotRef slot, int lane, int newIndex) {
    if (!hasParallel() || (lane != 0 && lane != 1)) return false;
    if (slot.type == sas::ModuleType::Splitter || slot.type == sas::ModuleType::Mixer || slot.type == sas::ModuleType::Amp || slot.type == sas::ModuleType::Cabinet) return false;
    const int at = indexInOrder(slot);
    if (at < 0) return false;
    const auto saved = rackOrder_;
    eraseEntry(at);
    newIndex = juce::jlimit(0, rackOrder_.count, newIndex);
    insertEntry(newIndex, slot, lane);
    const int s = rackOrder_.splitterIndex(), m = rackOrder_.mixerIndex();
    if (newIndex <= s || newIndex > m) {  // outside the parallel there are no lanes
        rackOrder_ = saved;
        return false;
    }
    normalizeLanes();
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::canAddParallel() const {
    int modules = 0;
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)].type != sas::ModuleType::Amp) ++modules;
    return !hasParallel() && modules + 2 <= RackState::kMaxModules;  // the splitter and the mixer
}

bool SasProcessor::addParallel() {
    using T = sas::ModuleType;
    if (!canAddParallel()) return false;
    const int ampAt0 = indexInOrder({T::Amp, 0}), cabAt = indexInOrder({T::Cabinet, 0});
    if (ampAt0 < 0 && cabAt < 0) return false;  // lane A is built around the amp or the cabinet
    const int ampAt = ampAt0 >= 0 ? ampAt0 : cabAt;
    // The splitter goes in front of the amp and of the compressor and overdrive that sit next to it (they keep working inside the amp, in lane A);
    // the mixer goes behind the cabinet. Lane B starts empty: what goes in it is the player's choice (a second amp and cabinet, a reverb on its own...).
    int start = ampAt;
    while (start > 0 && (rackOrder_.slots[std::size_t(start - 1)].type == T::Overdrive || rackOrder_.slots[std::size_t(start - 1)].type == T::Compressor)) --start;
    const int end = std::max(ampAt, cabAt);
    RackState::resetParams(apvts, {T::Mixer, 0});
    const auto saved = rackOrder_;
    insertEntry(end + 1, {T::Mixer, 0}, 0);
    insertEntry(start, {T::Splitter, 0}, 0);
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::removeParallel() {
    using T = sas::ModuleType;
    if (!hasParallel()) return false;
    std::vector<sas::SlotRef> gone;
    for (int i = rackOrder_.count - 1; i >= 0; --i) {
        const auto slot = rackOrder_.slots[std::size_t(i)];
        if (slot.type == T::Splitter || slot.type == T::Mixer || rackOrder_.lane[std::size_t(i)] != 0) {
            gone.push_back(slot);
            eraseEntry(i);
        }
    }
    syncRack(sas::Transition::Duck);
    for (const auto& slot : gone) {
        if (slot.type == T::NamPedal) clearNamPedal(slot.instance);
        RackState::resetParams(apvts, slot);
    }
    clearImpulseResponse(0, 1);
    clearImpulseResponse(1, 1);
    return true;
}

