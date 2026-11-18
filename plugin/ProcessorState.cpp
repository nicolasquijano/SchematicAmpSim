#include "ProcessorCommon.h"

// ------------------------------------------------------------------ state

// Slot A keeps the property names of the first versions ("irData" / "irName"), so older projects still load.
// The NAM pedal of the first instance keeps the property names of the first versions; the others add their number.
static juce::String namPedalKey(const char* base, int instance) { return instance == 0 ? juce::String(base) : juce::String(base) + juce::String(instance + 1); }

// The first cabinet keeps the property names of the first versions; the second one adds "C2".
static juce::String irDataKey(int cabinet, int slot) { return juce::String(slot == 0 ? "irData" : "irDataB") + (cabinet == 0 ? "" : "C2"); }
static juce::String irNameKey(int cabinet, int slot) { return juce::String(slot == 0 ? "irName" : "irNameB") + (cabinet == 0 ? "" : "C2"); }

void SasProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    {
        const juce::ScopedLock lock(irLock_);
        for (int cabinet = 0; cabinet < 2; ++cabinet)
            for (int s = 0; s < kIrSlots; ++s) {
                const auto& cb = cabs_[cabinet];
            state.removeProperty(irDataKey(cabinet, s), nullptr);
            state.removeProperty(irNameKey(cabinet, s), nullptr);
            if (cb.ir[s].bytes.getSize() > 0 && cb.ir[s].bytes.getSize() <= kMaxStoredIrBytes) {
                state.setProperty(irNameKey(cabinet, s), cb.ir[s].name, nullptr);
                state.setProperty(irDataKey(cabinet, s), juce::Base64::toBase64(cb.ir[s].bytes.getData(), cb.ir[s].bytes.getSize()), nullptr);
            }
        }
    }
    {
        const juce::ScopedLock lock(irLock_);
        for (int k = 0; k < kMultiSlots; ++k) {
            const juce::String dataKey = "mcData" + juce::String(k), nameKey = "mcName" + juce::String(k);
            state.removeProperty(dataKey, nullptr);
            state.removeProperty(nameKey, nullptr);
            if (multi_[k].bytes.getSize() > 0 && multi_[k].bytes.getSize() <= kMaxStoredIrBytes) {
                state.setProperty(nameKey, multi_[k].name, nullptr);
                state.setProperty(dataKey, juce::Base64::toBase64(multi_[k].bytes.getData(), multi_[k].bytes.getSize()), nullptr);
            }
        }
    }
    while (state.getChildWithName("PARTS").isValid()) state.removeChild(state.getChildWithName("PARTS"), nullptr);
    {
        const juce::ScopedLock lock(partsLock_);
        juce::ValueTree node("PARTS");
        for (std::size_t i = 0; i < defs_.size(); ++i) {
            if (parts_[i].empty()) continue;
            juce::ValueTree amp("AMP");
            amp.setProperty("id", juce::String(defs_[i].id), nullptr);
            for (const auto& [name, f] : parts_[i]) {
                juce::ValueTree part("PART");
                part.setProperty("n", juce::String(name), nullptr);
                part.setProperty("f", f, nullptr);
                amp.appendChild(part, nullptr);
            }
            node.appendChild(amp, nullptr);
        }
        if (node.getNumChildren() > 0) state.appendChild(node, nullptr);
    }
    while (state.getChildWithName("RACK").isValid()) state.removeChild(state.getChildWithName("RACK"), nullptr);
    state.appendChild(RackState::toTree(rackOrder_), nullptr);
    state.setProperty("rackVersion", RackState::kRackVersion, nullptr);
    state.removeProperty("namData", nullptr);
    state.removeProperty("namName", nullptr);
    state.removeProperty("namCabData", nullptr);
    state.removeProperty("namCabName", nullptr);
    for (int i = 0; i < kNamPedalSlots; ++i) {
        state.removeProperty(namPedalKey("namPedalData", i), nullptr);
        state.removeProperty(namPedalKey("namPedalName", i), nullptr);
    }
    {
        const juce::ScopedLock lock(namLock_);
        for (int i = 0; i < kNamPedalSlots; ++i) {
            const auto& slot = namPedals_[i];
            if (slot.bytes.getSize() > 0 && slot.bytes.getSize() <= kMaxStoredNamBytes) {
                state.setProperty(namPedalKey("namPedalName", i), slot.name, nullptr);
                state.setProperty(namPedalKey("namPedalData", i), juce::Base64::toBase64(slot.bytes.getData(), slot.bytes.getSize()), nullptr);
            }
        }
    }
    {
        const juce::ScopedLock lock(namLock_);
        if (namCabBytes_.getSize() > 0 && namCabBytes_.getSize() <= kMaxStoredNamBytes) {
            state.setProperty("namCabName", namCabName_, nullptr);
            state.setProperty("namCabData", juce::Base64::toBase64(namCabBytes_.getData(), namCabBytes_.getSize()), nullptr);
        }
    }
    {
        const juce::ScopedLock lock(namLock_);
        if (namBytes_.getSize() > 0 && namBytes_.getSize() <= kMaxStoredNamBytes) {
            state.setProperty("namName", namName_, nullptr);
            state.setProperty("namData", juce::Base64::toBase64(namBytes_.getData(), namBytes_.getSize()), nullptr);
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void SasProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    const auto tree = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(tree);
    restoreParts(tree);
    // A state from before the rack has no order: it comes from the old parameters.
    if (const auto rack = tree.getChildWithName("RACK"); rack.isValid()) rackOrder_ = RackState::fromTree(rack);
    else rackOrder_ = RackState::fromLegacy(apvts, tree.hasProperty("namPedalData"));
    syncRack(sas::Transition::Immediate);
    for (int cabinet = 0; cabinet < 2; ++cabinet)
        for (int s = 0; s < kIrSlots; ++s) {
            if (tree.hasProperty(irDataKey(cabinet, s))) {
                juce::MemoryOutputStream out;
                if (juce::Base64::convertFromBase64(out, tree[irDataKey(cabinet, s)].toString()) && out.getDataSize() > 0)
                    if (!loadImpulseResponseFromMemory(out.getData(), out.getDataSize(), tree[irNameKey(cabinet, s)].toString(), s, cabinet)) clearImpulseResponse(s, cabinet);
            } else {
                clearImpulseResponse(s, cabinet);
            }
        }
    for (int k = 0; k < kMultiSlots; ++k) {
        const juce::String dataKey = "mcData" + juce::String(k), nameKey = "mcName" + juce::String(k);
        if (tree.hasProperty(dataKey)) {
            juce::MemoryOutputStream out;
            if (juce::Base64::convertFromBase64(out, tree[dataKey].toString()) && out.getDataSize() > 0)
                if (!loadMultiImpulseFromMemory(out.getData(), out.getDataSize(), tree[nameKey].toString(), k)) clearMultiImpulse(k);
        } else {
            clearMultiImpulse(k);
        }
    }
    if (tree.hasProperty("namData")) {
        juce::MemoryOutputStream out;
        if (juce::Base64::convertFromBase64(out, tree["namData"].toString()) && out.getDataSize() > 0)
            if (!loadNamFromMemory(out.getData(), out.getDataSize(), tree["namName"].toString())) clearNamModel();
    } else {
        clearNamModel();
    }
    for (int i = 0; i < kNamPedalSlots; ++i) {
        if (tree.hasProperty(namPedalKey("namPedalData", i))) {
            juce::MemoryOutputStream out;
            if (juce::Base64::convertFromBase64(out, tree[namPedalKey("namPedalData", i)].toString()) && out.getDataSize() > 0)
                if (!loadNamPedalFromMemory(out.getData(), out.getDataSize(), tree[namPedalKey("namPedalName", i)].toString(), i)) clearNamPedal(i);
        } else {
            clearNamPedal(i);
        }
    }
    if (tree.hasProperty("namCabData")) {
        juce::MemoryOutputStream out;
        if (juce::Base64::convertFromBase64(out, tree["namCabData"].toString()) && out.getDataSize() > 0)
            if (!loadNamCabFromMemory(out.getData(), out.getDataSize(), tree["namCabName"].toString())) clearNamCab();
    } else {
        clearNamCab();
    }
}

void SasProcessor::applyEqPreset(int index, int instance) {
    if (index < 0 || index >= kEqPresetCount) return;
    const EqPreset& p = kEqPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::eqOn, 1.0);
    set(ids::eqHpf, p.hpfHz);
    set(ids::eqLpf, p.lpfHz);
    for (int b = 0; b < 5; ++b) {
        set(ids::eqFreq[b], p.band[std::size_t(b)].freq);
        set(ids::eqGain[b], p.band[std::size_t(b)].gainDb);
        set(ids::eqQ[b], p.band[std::size_t(b)].q);
    }
    // The presets are made of five bands; the three added ones are left flat at their own places.
    const sas::EqParams flat;
    for (int b = 5; b < sas::EqParams::kBands; ++b) {
        set(ids::eqFreq[b], flat.band[std::size_t(b)].freq);
        set(ids::eqGain[b], 0.0);
        set(ids::eqQ[b], flat.band[std::size_t(b)].q);
    }
}

void SasProcessor::applyTapePreset(int index, int instance) {
    if (index < 0 || index >= kTapePresetCount) return;
    const TapePreset& p = kTapePresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::tpOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    set(ids::tpTime, p.timeMs);
    set(ids::tpHeads, p.heads);
    set(ids::tpIntensity, p.intensity);
    set(ids::tpMix, p.mix);
    set(ids::tpTone, p.toneHz);
    set(ids::tpWow, p.wow);
    set(ids::tpFlutter, p.flutter);
    set(ids::tpSat, p.saturation);
    set(ids::tpStereo, p.stereo ? 1.0 : 0.0);
}

void SasProcessor::applyFuzzPreset(int index, int instance) {
    if (index < 0 || index >= kFuzzPresetCount) return;
    const FuzzPreset& p = kFuzzPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::fzOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    set(ids::fzType, p.type);
    set(ids::fzFuzz, p.fuzz);
    set(ids::fzTone, p.tone);
    set(ids::fzBias, p.bias);
    set(ids::fzMix, p.mix);
    set(ids::fzLevel, p.levelDb);
}

// The factory presets of the modules that have no function of their own (plugin/ModulePresets.h): they list the parameters they set.
void SasProcessor::applyModulePreset(sas::ModuleType type, int index, int instance) {
    const PresetList list = modulePresets(type);
    if (index < 0 || index >= list.count) return;
    const ModulePreset& preset = list.items[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    if (const char* on = RackState::onId(type)) {
        set(on, preset.on ? 1.0 : 0.0);
        if (!preset.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    }
    for (const auto& v : preset.v)
        if (v.id) set(v.id, v.value);
}

void SasProcessor::resetTubes(int instance) {
    if (auto* q = apvts.getParameter(RackState::paramId(ids::tube, instance))) {
        q->beginChangeGesture();
        q->setValueNotifyingHost(q->convertTo0to1(0.0f));  // 0 = the amp's own
        q->endChangeGesture();
    }
}

// A factory preset of the amp that instance `instance` has selected (amp.json "presets"): its knobs, its channel and its switches.
void SasProcessor::applyAmpPreset(int index, int instance) {
    const auto& def = defs_[std::size_t(selectedAmpIndex(instance))];
    if (index < 0 || index >= int(def.presets.size())) return;
    const sas::AmpPresetDef& preset = def.presets[std::size_t(index)];
    auto set = [this, instance](const juce::String& base, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(base.toRawUTF8(), instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    for (const auto& [param, value] : preset.params) set(param, value);
    if (!preset.jack.empty()) set(ids::jack, double(def.jackIndex(preset.jack)));
    for (const auto& [name, position] : preset.selectors)
        for (std::size_t i = 0; i < def.preamp.selectors.size() && i < 6; ++i)
            if (def.preamp.selectors[i].name == name) set(ids::sel[i], double(position));
}

void SasProcessor::applyDefaultAmpPreset(int instance) {
    const auto& def = defs_[std::size_t(selectedAmpIndex(instance))];
    const char* wanted = def.category == "clean" ? "Clean" : def.category == "crunch" ? "Crunch" : def.category == "highgain" ? "Lead" : nullptr;
    if (wanted == nullptr) return;
    for (std::size_t i = 0; i < def.presets.size(); ++i)
        if (def.presets[i].name == wanted) {
            applyAmpPreset(int(i), instance);
            return;
        }
}

void SasProcessor::applyWahPreset(int index, int instance) {
    if (index < 0 || index >= kWahPresetCount) return;
    const WahPreset& p = kWahPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::wahOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    set(ids::wahMode, p.mode);
    set(ids::wahFilter, p.filter);
    set(ids::wahPos, p.position);
    set(ids::wahHeel, p.heelHz);
    set(ids::wahToe, p.toeHz);
    set(ids::wahQ, p.q);
    set(ids::wahMix, p.mix);
    set(ids::wahLevel, p.levelDb);
    set(ids::wahSens, p.sensitivity);
    set(ids::wahAttack, p.attackMs);
    set(ids::wahRelease, p.releaseMs);
    set(ids::wahDir, p.down ? 1.0 : 0.0);
    set(ids::wahRate, p.rateHz);
    set(ids::wahStereo, p.stereo ? 1.0 : 0.0);
}

void SasProcessor::applyDelayPreset(int index, int instance) {
    if (index < 0 || index >= kDelayPresetCount) return;
    const DelayPreset& p = kDelayPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::dlyOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    set(ids::dlySync, 0.0);
    set(ids::dlyTime, p.timeMs);
    set(ids::dlyFeedback, p.feedback);
    set(ids::dlyMix, p.mix);
    set(ids::dlyTone, p.toneHz);
    set(ids::dlyPing, p.pingPong ? 1.0 : 0.0);
    set(ids::dlyAnalog, p.analog ? 1.0 : 0.0);
}

void SasProcessor::applyReverbPreset(int index, int instance) {
    if (index < 0 || index >= kReverbPresetCount) return;
    const ReverbPreset& p = kReverbPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::plOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;
    set(ids::plHall, p.hall ? 1.0 : 0.0);
    set(ids::plDecay, p.decaySec);
    set(ids::plPre, p.preMs);
    set(ids::plTone, p.toneHz);
    set(ids::plLow, p.lowCutHz);
    set(ids::plMix, p.mix);
    set(ids::plMod, p.mod);
    set(ids::plShimmer, p.shimmer);
    set(ids::plShimmerInt, p.shimmerFifth ? 1.0 : 0.0);
}

juce::File SasProcessor::presetFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Schematic Amp Sim").getChildFile("Presets");
}

bool SasProcessor::savePreset(const juce::File& file) {
    juce::MemoryBlock state;
    getStateInformation(state);
    const std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(state.getData(), int(state.getSize())));
    if (!xml) return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(xml->toString());
}

bool SasProcessor::loadPreset(const juce::File& file) {
    const std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return false;
    juce::MemoryBlock state;
    copyXmlToBinary(*xml, state);
    setStateInformation(state.getData(), int(state.getSize()));
    return true;
}

