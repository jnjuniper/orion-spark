#include "Overdrive.h"

namespace orion
{

namespace
{
    constexpr int oversamplingFactor = 4;
    constexpr float clipLimit = 0.5f;            // "diode" clipping level
    constexpr float referenceLevel = 0.18f;      // ~ -15 dBFS peak guitar
    constexpr float referenceHighPassGain = 0.3f; // approx. 720 Hz HP gain on low-mid fundamentals

    float gainForDrive (float drive) { return 12.0f * std::pow (10.0f, drive); } // 12x .. 120x

    // Keeps a reference-level signal roughly at unity, so Drive changes the
    // character more than the volume (Level sets the volume).
    float makeupFor (float g)
    {
        const float clipped = clipLimit * std::tanh ((g - 1.0f) * referenceLevel * referenceHighPassGain / clipLimit);
        return referenceLevel / (referenceLevel + clipped);
    }
}

Overdrive::Overdrive()
    : Pedal ("od", "Overdrive", "Mid boost", "OD", juce::Colour (0xff3d6b3a), false)
{
}

void Overdrive::addPedalParameters (Layout& layout)
{
    const auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    addFloat (layout, "drive", "Drive", { 0.0f, 1.0f }, 0.3f, percent);
    addFloat (layout, "tone", "Tone", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "level", "Level", { -18.0f, 18.0f, 0.1f }, 0.0f,
              [] (float v) { return juce::String (v, 1) + " dB"; });
}

void Overdrive::attachPedal (juce::AudioProcessorValueTreeState& state)
{
    driveParam = param (state, "drive");
    toneParam  = param (state, "tone");
    levelParam = param (state, "level");
}

void Overdrive::preparePedal (double sr, int maxBlockSize)
{
    oversampling.initProcessing ((size_t) maxBlockSize);
    latency = (int) std::round (oversampling.getLatencyInSamples());

    for (int ch = 0; ch < 2; ++ch)
    {
        inputHighPass[ch].setHighPass (sr, 90.0, 0.6);  // tighten the lows a little
        clipHighPass[ch].setCutoff (sr * oversamplingFactor, 720.0);
    }

    clipGain.reset (sr * oversamplingFactor, 0.03);
    outputGain.reset (sr, 0.03);
    lastTone = -1.0f;
    resetPedal();
}

void Overdrive::resetPedal()
{
    oversampling.reset();

    for (int ch = 0; ch < 2; ++ch)
    {
        inputHighPass[ch].reset();
        toneLowPass[ch].reset();
        clipHighPass[ch].reset();
    }

    const float g = gainForDrive (driveParam != nullptr ? driveParam->load() : 0.3f);
    const float level = levelParam != nullptr ? levelParam->load() : 0.0f;
    clipGain.setCurrentAndTargetValue (g - 1.0f);
    outputGain.setCurrentAndTargetValue (dsp::dbToGain (level) * makeupFor (g));
}

void Overdrive::processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float*)
{
    const float g = gainForDrive (driveParam->load());
    clipGain.setTargetValue (g - 1.0f);
    outputGain.setTargetValue (dsp::dbToGain (levelParam->load()) * makeupFor (g));

    if (const float tone = toneParam->load(); ! juce::exactlyEqual (tone, lastTone))
    {
        lastTone = tone;
        const double cutoff = 700.0 * std::pow (5000.0 / 700.0, (double) tone);
        for (auto& f : toneLowPass)
            f.setLowPass (sampleRate, cutoff, 0.6);
    }

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = inputHighPass[0].process (left[i]);
        right[i] = inputHighPass[1].process (right[i]);
    }

    // Dry + clipped high-passed copy, at 4x.
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
        auto up = oversampling.processSamplesUp (block);
        auto* l = up.getChannelPointer (0);
        auto* r = up.getChannelPointer (1);
        const float invLimit = 1.0f / clipLimit;

        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float gm1 = clipGain.getNextValue();
            l[i] += clipLimit * std::tanh (gm1 * clipHighPass[0].process (l[i]) * invLimit);
            r[i] += clipLimit * std::tanh (gm1 * clipHighPass[1].process (r[i]) * invLimit);
        }

        oversampling.processSamplesDown (block);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float out = outputGain.getNextValue();
        left[i]  = toneLowPass[0].process (left[i]) * out;
        right[i] = toneLowPass[1].process (right[i]) * out;
    }
}

} // namespace orion
