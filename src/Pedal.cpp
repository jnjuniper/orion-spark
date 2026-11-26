#include "Pedal.h"

namespace orion
{

Pedal::Pedal (juce::String pedalId, juce::String displayName, juce::String sub, juce::String badgeText,
              juce::Colour c, bool on)
    : id (std::move (pedalId)), name (std::move (displayName)), subtitle (std::move (sub)),
      badge (std::move (badgeText)), colour (c), defaultOn (on)
{
}

void Pedal::addParameters (Layout& layout)
{
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { getOnParamId(), 1 },
                                                            name + " On", defaultOn));
    addPedalParameters (layout);
}

void Pedal::attach (juce::AudioProcessorValueTreeState& state)
{
    onParam = state.getRawParameterValue (getOnParamId());
    jassert (onParam != nullptr);
    attachPedal (state);
}

void Pedal::addFloat (Layout& layout, const juce::String& suffix, const juce::String& knobLabel,
                      juce::NormalisableRange<float> range, float defaultValue,
                      std::function<juce::String (float)> toText)
{
    const auto paramId = id + "_" + suffix;
    auto attributes = juce::AudioParameterFloatAttributes();

    if (toText)
        attributes = attributes.withStringFromValueFunction ([toText] (float v, int) { return toText (v); });

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { paramId, 1 },
                                                             name + " " + knobLabel,
                                                             range, defaultValue, attributes));
    knobs.push_back ({ paramId, knobLabel });
}

void Pedal::addSwitch (Layout& layout, const juce::String& suffix, const juce::String& switchLabel, bool defaultValue)
{
    const auto paramId = id + "_" + suffix;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { paramId, 1 },
                                                            name + " " + switchLabel, defaultValue));
    switches.push_back ({ paramId, switchLabel });
}

std::atomic<float>* Pedal::param (juce::AudioProcessorValueTreeState& state, const juce::String& suffix) const
{
    auto* p = state.getRawParameterValue (id + "_" + suffix);
    jassert (p != nullptr);
    return p;
}

void Pedal::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr;
    preparePedal (sr, maxBlockSize);

    dryBuffer.setSize (2, maxBlockSize, false, true, true);
    rampBuffer.assign ((size_t) maxBlockSize, 0.0f);

    const int latency = getLatencySamples();
    dryDelay.setSize (2, juce::jmax (1, latency), false, true, true);
    dryDelay.clear();
    dryDelayPos = 0;

    onRamp.reset (sr, 0.015);
    const bool on = onParam != nullptr ? onParam->load() > 0.5f : defaultOn;
    onRamp.setCurrentAndTargetValue (on ? 1.0f : 0.0f);
}

void Pedal::reset()
{
    resetPedal();
    dryDelay.clear();
    dryDelayPos = 0;
}

void Pedal::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    jassert (buffer.getNumChannels() >= 2);
    jassert (numSamples <= (int) rampBuffer.size());

    const bool on = onParam != nullptr ? onParam->load() > 0.5f : defaultOn;
    onRamp.setTargetValue (on ? 1.0f : 0.0f);

    const bool rampMoving = onRamp.isSmoothing();
    for (int i = 0; i < numSamples; ++i)
        rampBuffer[(size_t) i] = onRamp.getNextValue();

    if (usesTrails())
    {
        processPedal (buffer, numSamples, rampBuffer.data());
        return;
    }

    const int latency = getLatencySamples();
    const bool fullyOff = ! rampMoving && ! on;
    const bool fullyOn  = ! rampMoving && on;

    if (fullyOff && latency == 0)
        return;

    // Keep a (latency-aligned) copy of the dry signal for the crossfade / bypass.
    if (latency > 0)
    {
        int pos = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* in   = buffer.getReadPointer (ch);
            auto* dry  = dryBuffer.getWritePointer (ch);
            auto* line = dryDelay.getWritePointer (ch);
            pos = dryDelayPos;

            for (int i = 0; i < numSamples; ++i)
            {
                dry[i] = line[pos];
                line[pos] = in[i];
                if (++pos >= latency)
                    pos = 0;
            }
        }
        dryDelayPos = pos;
    }
    else if (! fullyOn)
    {
        for (int ch = 0; ch < 2; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);
    }

    processPedal (buffer, numSamples, nullptr);

    if (fullyOn)
        return;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* out = buffer.getWritePointer (ch);
        auto* dry = dryBuffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float r = rampBuffer[(size_t) i];
            out[i] = dry[i] + r * (out[i] - dry[i]);
        }
    }
}

} // namespace orion
