#include "NoiseGate.h"
#include "../DspUtils.h"

namespace orion
{

namespace
{
    float coeffForTime (double sr, double seconds)
    {
        return (float) std::exp (-1.0 / (juce::jmax (1.0e-4, seconds) * sr));
    }
}

NoiseGate::NoiseGate()
    : Pedal ("gate", "Gate", "Noise gate", "GATE", juce::Colour (0xff3a4148))
{
}

void NoiseGate::addPedalParameters (Layout& layout)
{
    addFloat (layout, "threshold", "Threshold", { -90.0f, -20.0f, 0.1f }, -62.0f,
              [] (float v) { return juce::String (v, 1) + " dB"; });
    addFloat (layout, "release", "Release", { 5.0f, 500.0f, 0.1f, 0.4f }, 80.0f,
              [] (float v) { return juce::String (v, 0) + " ms"; });
}

void NoiseGate::attachPedal (juce::AudioProcessorValueTreeState& state)
{
    thresholdParam = param (state, "threshold");
    releaseParam = param (state, "release");
}

void NoiseGate::preparePedal (double sr, int)
{
    envReleaseCoeff = coeffForTime (sr, 0.010);
    attackCoeff = coeffForTime (sr, 0.0005);
    holdSamples = (int) (0.030 * sr);
    resetPedal();
}

void NoiseGate::resetPedal()
{
    envelope = 0.0f;
    gain = 0.0f;
    open = false;
    holdCounter = 0;
}

void NoiseGate::processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float*)
{
    const float openThreshold  = dsp::dbToGain (thresholdParam->load());
    const float closeThreshold = openThreshold * 0.5f; // 6 dB hysteresis
    const float releaseCoeff   = coeffForTime (sampleRate, releaseParam->load() * 0.001);

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const float level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
        envelope = level > envelope ? level : envelope * envReleaseCoeff;

        if (envelope >= openThreshold)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (open && envelope < closeThreshold)
        {
            if (--holdCounter <= 0)
                open = false;
        }

        const float target = open ? 1.0f : 0.0f;
        const float coeff = target > gain ? attackCoeff : releaseCoeff;
        gain = target + coeff * (gain - target);

        left[i]  *= gain;
        right[i] *= gain;
    }
}

} // namespace orion
