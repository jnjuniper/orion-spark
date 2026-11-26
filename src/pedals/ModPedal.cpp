#include "ModPedal.h"

namespace orion
{

namespace
{
    // Chorus: 12 ms base, sweeps up to +6 ms. Flanger: 1 ms base, sweeps up to +3 ms.
    constexpr float chorusBaseMs = 12.0f, chorusDepthMs = 6.0f;
    constexpr float flangerBaseMs = 1.0f, flangerDepthMs = 3.0f;
    constexpr float chorusMaxFeedback = 0.15f, flangerMaxFeedback = 0.75f;
    constexpr float maxDelayMs = chorusBaseMs + chorusDepthMs + 2.0f;
}

ModPedal::ModPedal()
    : Pedal ("mod", "Mod", "Chorus / flanger", "MOD", juce::Colour (0xff3b4a6b), false)
{
}

void ModPedal::addPedalParameters (Layout& layout)
{
    const auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    addFloat (layout, "rate", "Rate", { 0.05f, 8.0f, 0.01f, 0.4f }, 0.6f,
              [] (float v) { return juce::String (v, 2) + " Hz"; });
    addFloat (layout, "depth", "Depth", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "regen", "Regen", { 0.0f, 1.0f }, 0.3f, percent);
    addFloat (layout, "mix", "Mix", { 0.0f, 1.0f }, 0.5f, percent);
    addSwitch (layout, "flanger", "Flanger", false);
}

void ModPedal::attachPedal (juce::AudioProcessorValueTreeState& state)
{
    rateParam    = param (state, "rate");
    depthParam   = param (state, "depth");
    regenParam   = param (state, "regen");
    mixParam     = param (state, "mix");
    flangerParam = param (state, "flanger");
}

void ModPedal::preparePedal (double sr, int)
{
    for (auto& d : delay)
        d.prepare ((int) std::ceil (maxDelayMs * 0.001 * sr) + 8);

    mode.reset (sr, 0.08);
    for (auto* s : { &depth, &regen, &mix, &rate })
        s->reset (sr, 0.05);

    resetPedal();
}

void ModPedal::resetPedal()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        delay[ch].reset();
        lfo[ch].reset (ch == 0 ? 0.0f : 0.25f);   // 90 degrees apart
        feedbackState[ch] = 0.0f;
    }

    mode.setCurrentAndTargetValue (flangerParam != nullptr && flangerParam->load() > 0.5f ? 1.0f : 0.0f);
    depth.setCurrentAndTargetValue (depthParam != nullptr ? depthParam->load() : 0.5f);
    regen.setCurrentAndTargetValue (regenParam != nullptr ? regenParam->load() : 0.3f);
    mix.setCurrentAndTargetValue (mixParam != nullptr ? mixParam->load() : 0.5f);
    rate.setCurrentAndTargetValue (rateParam != nullptr ? rateParam->load() : 0.6f);
}

void ModPedal::processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float*)
{
    mode.setTargetValue (flangerParam->load() > 0.5f ? 1.0f : 0.0f);
    depth.setTargetValue (depthParam->load());
    regen.setTargetValue (regenParam->load());
    mix.setTargetValue (mixParam->load());
    rate.setTargetValue (rateParam->load());

    const float msToSamples = (float) (0.001 * sampleRate);
    float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = mode.getNextValue();
        const float dep = depth.getNextValue();
        const float fb = regen.getNextValue() * (chorusMaxFeedback + m * (flangerMaxFeedback - chorusMaxFeedback));
        const float wetGain = mix.getNextValue();
        const float r = rate.getNextValue();

        const float baseMs  = chorusBaseMs + m * (flangerBaseMs - chorusBaseMs);
        const float depthMs = (chorusDepthMs + m * (flangerDepthMs - chorusDepthMs)) * dep;
        const float norm = 1.0f / (1.0f + 0.5f * wetGain);

        for (int ch = 0; ch < 2; ++ch)
        {
            lfo[ch].setRate (sampleRate, r);
            const float sweep = 0.5f * (1.0f + lfo[ch].next());
            const float delaySamples = (baseMs + depthMs * sweep) * msToSamples;

            const float dry = io[ch][i];
            delay[ch].push (dry + fb * feedbackState[ch]);
            const float wet = delay[ch].readCubic (delaySamples);
            feedbackState[ch] = dsp::softClip (wet, 1.0f);   // |.| < 1, loop gain <= 0.75

            io[ch][i] = (dry + wetGain * wet) * norm;
        }
    }
}

} // namespace orion
