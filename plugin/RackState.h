#pragma once
// The rack's parameters and saved state (docs/MODULAR_RACK.md, section 4): one group of host parameters per module instance (instance 0 keeps the
// ids of the fixed-order plug-in, so old automation, projects and presets still point at the same parameters), and the order of the modules as a
// <RACK> node inside the project state.

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

#include "sas/rack.h"

namespace RackState {

inline constexpr int kParamVersion = 1;
inline constexpr int kRackVersion = 1;
inline constexpr int kMaxModules = 12;  // the amp is not counted

// How many instances of a type may exist (the amp and the cabinet are single).
int maxInstances(sas::ModuleType type);
// Instance 0 keeps `baseId`; the others add "__2", "__3"... and a " 2", " 3" on the display name.
juce::String paramId(const char* baseId, int instance);
juce::String paramName(const char* baseName, int instance);
// The base ids of every parameter of a module type (empty for the amp and the cabinet, whose parameters are the plug-in's own).
const std::vector<const char*>& paramIds(sas::ModuleType type);
// The base id of the type's On switch (nullptr when it has none).
const char* onId(sas::ModuleType type);

// Adds the parameters of one instance to the layout.
void addParams(juce::AudioProcessorValueTreeState::ParameterLayout& layout, sas::ModuleType type, int instance);
// Adds every instance beyond the first (appended at the end of the layout).
void addExtraInstances(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

// 0..10 knob shown with one decimal, like the amp's dial; a +-24 dB gain.
std::unique_ptr<juce::AudioParameterFloat> knob(const juce::String& id, const juce::String& name, float def);
std::unique_ptr<juce::AudioParameterFloat> dbParam(const juce::String& id, const juce::String& name, float defaultDb = 0.0f);

// The modules that are always in the rack and cannot be removed: the amp, the first noise gate and the cabinet.
bool isFixed(sas::SlotRef slot);
// What a new instance of the plug-in starts with: the fixed modules, gate, amp, cabinet.
sas::RackOrder defaultOrder();
// The rack the fixed-order plug-in's parameters describe (used for states saved before the rack existed). `namPedalLoaded`: the state carries a model
// for the NAM pedal, which keeps the module even when it is off.
sas::RackOrder fromLegacy(const juce::AudioProcessorValueTreeState& apvts, bool namPedalLoaded, bool everyModule = false);
juce::ValueTree toTree(const sas::RackOrder& order);
// Tolerant: unknown types, repeated or out-of-range instances and anything beyond the limits are dropped; the amp is always there.
sas::RackOrder fromTree(const juce::ValueTree& rack);
// Puts a module's parameters back to their defaults (a module that has just been added).
void resetParams(juce::AudioProcessorValueTreeState& apvts, sas::SlotRef slot);

}  // namespace RackState
