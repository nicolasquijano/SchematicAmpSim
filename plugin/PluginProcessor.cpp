#include "PluginProcessor.h"

AesProcessor::AesProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void AesProcessor::prepareToPlay(double, int) {}

bool AesProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void AesProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    // F0 skeleton: audio passes through untouched.
}

juce::AudioProcessorEditor* AesProcessor::createEditor() {
    return new juce::GenericAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AesProcessor(); }
