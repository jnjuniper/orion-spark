#include "SpringReverb.h"

namespace orion
{

namespace
{
    constexpr double springLengthsSeconds[2] = { 0.0371, 0.0433 };
    constexpr float minRt60 = 0.6f, maxRt60 = 5.0f;
}

SpringReverb::SpringReverb()
    : Pedal ("spring", "Spring", "Spring reverb", "SPRING", juce::Colour (0xff7a5a2e))
{
}

void SpringReverb::addPedalParameters (Layout& layout)
{
    const auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    addFloat (layout, "decay", "Decay", { 0.0f, 1.0f }, 0.45f, percent);
    addFloat (layout, "tone", "Tone", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "drip", "Drip", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "mix", "Mix", { 0.0f, 1.0f }, 0.25f, percent);
}

void SpringReverb::attachPedal (juce::AudioProcessorValueTreeState& state)
{
    decayParam = param (state, "decay");
    toneParam  = param (state, "tone");
    dripParam  = param (state, "drip");
    mixParam   = param (state, "mix");
}

void SpringReverb::preparePedal (double sr, int)
{
    // Spring tanks don't pass much deep bass or extreme treble.
    inputHighPass.setHighPass (sr, 180.0);
    inputLowPass.setLowPass (sr, 5000.0);

    for (int s = 0; s < 2; ++s)
    {
        auto& spring = springs[(size_t) s];
        spring.delaySamples = juce::jmax (1, (int) std::round (springLengthsSeconds[s] * sr));
        spring.delay.prepare (spring.delaySamples + 8);
        spring.damping.setCutoff (sr, 4200.0);
        spring.shimmer.setRate (sr, s == 0 ? 0.7 : 0.93);
        spring.allpassOffset = s == 0 ? 0.0f : 0.04f;
    }

    mix.reset (sr, 0.05);
    drip.reset (sr, 0.05);
    lastTone = -1.0f;
    resetPedal();
}

void SpringReverb::resetPedal()
{
    inputHighPass.reset();
    inputLowPass.reset();

    for (auto& spring : springs)
    {
        for (auto& ap : spring.dispersion)
            ap.reset();
        spring.delay.reset();
        spring.damping.reset();
        spring.outputTone.reset();
        spring.shimmer.reset();
        spring.loopState = 0.0f;
    }

    mix.setCurrentAndTargetValue (mixParam != nullptr ? mixParam->load() : 0.0f);
    drip.setCurrentAndTargetValue (dripParam != nullptr ? dripParam->load() : 0.5f);
}

void SpringReverb::processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain)
{
    const float rt60 = minRt60 * std::pow (maxRt60 / minRt60, decayParam->load());
    mix.setTargetValue (mixParam->load());
    drip.setTargetValue (dripParam->load());

    if (const float tone = toneParam->load(); ! juce::exactlyEqual (tone, lastTone))
    {
        lastTone = tone;
        const double cutoff = 2200.0 * std::pow (4.0, (double) tone); // 2.2 kHz .. 8.8 kHz
        for (auto& spring : springs)
            spring.outputTone.setLowPass (sampleRate, cutoff, 0.6);
    }

    // Per-spring feedback gain for the requested RT60, plus output normalisation
    // so the reverb level doesn't jump as decay changes.
    float loopGain[2], norm[2];
    for (int s = 0; s < 2; ++s)
    {
        const float loopSeconds = (float) springs[(size_t) s].delaySamples / (float) sampleRate;
        loopGain[s] = std::pow (10.0f, -3.0f * loopSeconds / rt60);
        norm[s] = std::sqrt (1.0f - loopGain[s] * loopGain[s]) * 2.5f;
    }

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float in = inputLowPass.process (inputHighPass.process (0.5f * (left[i] + right[i]) * sendGain[i]));
        const float dripAmount = drip.getNextValue();
        const float wetGain = mix.getNextValue();

        float wet[2];
        for (int s = 0; s < 2; ++s)
        {
            auto& spring = springs[(size_t) s];
            const float a = juce::jlimit (-0.9f, 0.9f, 0.3f + 0.45f * dripAmount + spring.allpassOffset
                                                           + 0.01f * spring.shimmer.next());

            float x = in + loopGain[s] * spring.loopState;
            for (auto& ap : spring.dispersion)
                x = ap.process (x, a);

            spring.delay.push (x);
            const float y = spring.delay.readInt (spring.delaySamples - 1);
            spring.loopState = spring.damping.process (y);
            wet[s] = spring.outputTone.process (y) * norm[s];
        }

        left[i]  += wetGain * wet[0];
        right[i] += wetGain * wet[1];
    }
}

} // namespace orion
