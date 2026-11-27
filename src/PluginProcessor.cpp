#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "pedals/NoiseGate.h"
#include "pedals/Overdrive.h"
#include "pedals/BritStack.h"
#include "pedals/FourTrackPre.h"
#include "pedals/ModPedal.h"
#include "pedals/TapeEcho.h"
#include "pedals/SpringReverb.h"

OrionAudioProcessor::OrionAudioProcessor (orion::UserPresetLibrary userPresetLibrary)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      pedals (createPedals()),
      state (*this, nullptr, "OrionState", createLayout()),
      userPresets (std::move (userPresetLibrary))
{
    for (auto& pedal : pedals)
        pedal->attach (state);

    inputGainParam = state.getRawParameterValue (inputGainId);
    outputGainParam = state.getRawParameterValue (outputGainId);

    setCurrentPreset ({ false, orion::getFactoryPresets().front().name });   // all defaults = "Init"
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

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputGainParam->load()));

    // Hosts occasionally send larger blocks than announced; process in chunks.
    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int len = juce::jmin (maxBlockSize, numSamples - start);
        juce::AudioBuffer<float> chunk (buffer.getArrayOfWritePointers(), 2, start, len);

        inputGain.applyGain (chunk, len);

        for (auto& pedal : pedals)
            pedal->process (chunk, len);

        outputGain.applyGain (chunk, len);
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

// ---------------------------------------------------------------------------
// Presets
//
// The current preset is saved in the state as a name plus whether it's a user
// preset, never as a list position: user presets come and go, which would shift
// positions. Sessions from before user presets stored a factory index ("preset").

namespace
{
    constexpr const char* presetNameProperty = "presetName";
    constexpr const char* presetIsUserProperty = "presetIsUser";
    constexpr const char* legacyPresetIndexProperty = "preset";
}

OrionAudioProcessor::PresetRef OrionAudioProcessor::getCurrentPreset() const
{
    const juce::SpinLock::ScopedLockType lock (presetLock);
    return currentPreset;
}

void OrionAudioProcessor::setCurrentPreset (PresetRef ref)
{
    {
        const juce::SpinLock::ScopedLockType lock (presetLock);
        std::swap (currentPreset, ref);
    }

    ++presetChangeCount;
}

void OrionAudioProcessor::loadFactoryPreset (int index)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto& presets = orion::getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    const auto& preset = presets[(size_t) index];
    orion::applyPreset (*this, preset);
    presetBaseline = orion::resolvePreset (*this, preset);
    presetBaselineKnown = true;
    presetBaselineStale = false;
    setCurrentPreset ({ false, preset.name });
}

bool OrionAudioProcessor::loadUserPreset (const juce::String& name)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto preset = userPresets.load (name);
    if (! preset)
        return false;

    orion::applyPreset (*this, *preset);
    presetBaseline = orion::resolvePreset (*this, *preset);
    presetBaselineKnown = true;
    presetBaselineStale = false;
    setCurrentPreset ({ true, preset->name });
    return true;
}

bool OrionAudioProcessor::saveUserPreset (const juce::String& name)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto cleaned = orion::UserPresetLibrary::cleanName (name);
    if (cleaned.isEmpty())
        return false;

    const auto preset = orion::capturePreset (*this, cleaned);
    if (! userPresets.save (preset))
        return false;

    presetBaseline = orion::resolvePreset (*this, preset);
    presetBaselineKnown = true;
    presetBaselineStale = false;
    setCurrentPreset ({ true, cleaned });
    return true;
}

bool OrionAudioProcessor::renameUserPreset (const juce::String& from, const juce::String& to)
{
    JUCE_ASSERT_MESSAGE_THREAD
    const auto cleaned = orion::UserPresetLibrary::cleanName (to);
    if (! userPresets.rename (from, cleaned))
        return false;

    const auto current = getCurrentPreset();
    if (current.isUser && current.name.equalsIgnoreCase (from))
        setCurrentPreset ({ true, cleaned });
    else
        ++presetChangeCount;   // the library changed

    return true;
}

bool OrionAudioProcessor::deleteUserPreset (const juce::String& name)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! userPresets.remove (name))
        return false;

    // The board keeps its sound and name; it now shows as unsaved.
    presetBaselineStale = true;
    ++presetChangeCount;
    return true;
}

void OrionAudioProcessor::rebuildPresetBaseline()
{
    const auto ref = getCurrentPreset();
    std::optional<orion::Preset> preset;

    if (ref.isUser)
        preset = userPresets.load (ref.name);
    else if (const int index = orion::findFactoryPreset (ref.name); index >= 0)
        preset = orion::getFactoryPresets()[(size_t) index];

    presetBaselineKnown = preset.has_value();
    presetBaseline = preset ? orion::resolvePreset (*this, *preset)
                            : std::vector<std::pair<juce::RangedAudioParameter*, float>>();
}

bool OrionAudioProcessor::isPresetModified()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (presetBaselineStale.exchange (false))
        rebuildPresetBaseline();

    if (! presetBaselineKnown)
        return true;

    for (const auto& [param, value] : presetBaseline)
        if (std::abs (param->getValue() - value) > 1.0e-4f)
            return true;

    return false;
}

juce::AudioProcessorEditor* OrionAudioProcessor::createEditor()
{
    return new OrionAudioProcessorEditor (*this);
}

void OrionAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    const auto preset = getCurrentPreset();
    tree.setProperty (presetNameProperty, preset.name, nullptr);
    tree.setProperty (presetIsUserProperty, preset.isUser, nullptr);
    tree.removeProperty (legacyPresetIndexProperty, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void OrionAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            state.replaceState (tree);

            PresetRef ref;
            if (tree.hasProperty (presetNameProperty))
            {
                ref.name = tree[presetNameProperty].toString();
                ref.isUser = (bool) tree[presetIsUserProperty];
            }
            else
            {
                const auto& factory = orion::getFactoryPresets();
                const int index = juce::jlimit (0, (int) factory.size() - 1, (int) tree.getProperty (legacyPresetIndexProperty, 0));
                ref.name = factory[(size_t) index].name;
            }

            setCurrentPreset (std::move (ref));
            presetBaselineStale = true;   // may be off the message thread; rebuilt on next use
        }
}

#ifndef ORION_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrionAudioProcessor();
}
#endif
