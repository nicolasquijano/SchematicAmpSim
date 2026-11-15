#pragma once
// What the PluginProcessor*.cpp files share: the includes and the limits (each file gets its own copy of the constants).
#include "PluginProcessor.h"

#include "FxPresets.h"
#include "ModulePresets.h"

#include "CabData.h"

#include <algorithm>
#include <array>

#include "PluginEditor.h"
#include "CabinetModule.h"
#include "RackState.h"
#include "EqPresets.h"
#include "sas/embedded_amps.h"

namespace {

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
constexpr int kVersion = 1;

using RackState::dbParam;
using RackState::knob;

constexpr std::size_t kMaxChunk = 4096;
constexpr std::size_t kMaxStoredIrBytes = 4 * 1024 * 1024;
constexpr double kMaxIrSeconds = 4.0;
constexpr std::size_t kMaxStoredNamBytes = 16 * 1024 * 1024;

}  // namespace

// Decodes an audio file (from memory) into one levelled channel for the convolutions: the first channel, cut to kMaxIrSeconds, at unity gain over
// the guitar band. `curve` is its magnitude response for the editor. False when the file is not audio or is silent.
bool decodeImpulse(const void* data, std::size_t size, juce::AudioBuffer<float>& mono, double& rate, std::vector<float>& curve);
// The magnitude response of an impulse in dB (see decodeImpulse).
std::vector<float> impulseCurve(const juce::AudioBuffer<float>& mono, double rate);
