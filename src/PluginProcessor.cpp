#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include "pedals/NoiseGate.h"
#include "pedals/Overdrive.h"
#include "pedals/BritStack.h"
#include "pedals/FourTrackPre.h"
#include "pedals/ModPedal.h"
#include "pedals/TapeEcho.h"
#include "pedals/SpringReverb.h"

OrionAudioProcessor::OrionAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      pedals (createPedals()),
      state (*this, nullptr, "OrionState", createLayout())
{
    for (auto& pedal : pedals)
        pedal->attach (state);

    inputGainParam = state.getRawParameterValue (inputGainId);
    outputGainParam = state.getRawParameterValue (outputGainId);
}

std::vector<std::unique_ptr<orion::Pedal>> OrionAudioProcessor::createPedals()
{
    std::vector<std::unique_ptr<orion::Pedal>> chain;
    chain.push_back (std::make_unique<orion::NoiseGate>());
    chain.push_back (std::make_unique<orion::Overdrive>());
    chain.push_back (std::make_unique<orion::BritStack>());
    chain.push_back (std::make_unique<orion::FourTrackPre>());
    chain.push_back (std::make_unique<orion::ModPedal>());
    chain.push_back (std::make_unique<orion::TapeEcho>());
    chain.push_back (std::make_unique<orion::SpringReverb>());
    return chain;
}

juce::AudioProcessorValueTreeState::ParameterLayout OrionAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto dbText = juce::AudioParameterFloatAttributes()
                            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { inputGainId, 1 }, "Input",
                                                             juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f, dbText));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { outputGainId, 1 }, "Output",
                                                             juce::NormalisableRange<float> { -36.0f, 12.0f, 0.1f }, 0.0f, dbText));

    for (auto& pedal : pedals)
        pedal->addParameters (layout);

    return layout;
}

bool OrionAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void OrionAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    int latency = 0;
    for (auto& pedal : pedals)
    {
        pedal->prepare (sampleRate, maxBlockSize);
        pedal->reset();
        latency += pedal->getLatencySamples();
    }
    setLatencySamples (latency);

    inputTap.setSampleRate (sampleRate);
    for (auto* gain : { &tunerInputGain, &tunerOutputGain })
    {
        gain->reset (sampleRate, 0.02);
        gain->setCurrentAndTargetValue (tunerActive.load() ? 0.0f : 1.0f);
    }

    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));
}

void OrionAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputs = getTotalNumInputChannels();

    if (buffer.getNumChannels() < 2)
        return;

    // Mono guitar in -> process in stereo.
    if (numInputs == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    else if (numInputs == 0)
        buffer.clear();

    // The tuner listens to the dry guitar, before the gate and drives.
    inputTap.push (buffer.getReadPointer (0), numInputs >= 2 ? buffer.getReadPointer (1) : nullptr, numSamples);
    const float tunerTarget = tunerActive.load() ? 0.0f : 1.0f;
    tunerInputGain.setTargetValue (tunerTarget);
    tunerOutputGain.setTargetValue (tunerTarget);

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));

    // Hosts occasionally send larger blocks than announced; process in chunks.
    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int len = juce::jmin (maxBlockSize, numSamples - start);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), 2, start, len);

        inputGain.applyGain (chunk, len);
        tunerInputGain.applyGain (chunk, len);

        for (auto& pedal : pedals)
            pedal->process (chunk, len);

        outputGain.applyGain (chunk, len);
        tunerOutputGain.applyGain (chunk, len);   // the pedals keep running, so latency is unchanged
    }

    // Last line of defence: never pass NaN/Inf to the host or your ears.
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }
}

int OrionAudioProcessor::getNumPresets() const
{
    return (int) orion::getFactoryPresets().size();
}

void OrionAudioProcessor::loadPreset (int index)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto& presets = orion::getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    orion::applyPreset (state, presets[(size_t) index]);
    currentPreset = index;
    state.state.setProperty ("preset", index, nullptr);
}

juce::AudioProcessorEditor* OrionAudioProcessor::createEditor()
{
    return new OrionAudioProcessorEditor (*this);
}

void OrionAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OrionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            state.replaceState (juce::ValueTree::fromXml (*xml));
            currentPreset = (int) state.state.getProperty ("preset", 0);
        }
}

#ifndef ORION_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrionAudioProcessor();
}
#endif
