#include "TapeEcho.h"

namespace orion
{

namespace
{
    constexpr float maxTimeMs = 1200.0f;
    constexpr float maxLoopGain = 0.97f;          // hard safety cap, always < 1
    constexpr double maxWobbleSeconds = 0.0025;
}

TapeEcho::TapeEcho()
    : Pedal ("echo", "Tape Echo", "Tape delay", "ECHO", juce::Colour (0xff8a4423))
{
}

void TapeEcho::addPedalParameters (Layout& layout)
{
    const auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    addFloat (layout, "time", "Time", { 40.0f, maxTimeMs, 1.0f, 0.5f }, 380.0f,
              [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
    addFloat (layout, "repeats", "Repeats", { 0.0f, 1.0f }, 0.4f, percent);
    addFloat (layout, "mix", "Mix", { 0.0f, 1.0f }, 0.3f, percent);
    addFloat (layout, "age", "Age", { 0.0f, 1.0f }, 0.4f, percent);
    addFloat (layout, "wobble", "Wobble", { 0.0f, 1.0f }, 0.2f, percent);
}

void TapeEcho::attachPedal (juce::AudioProcessorValueTreeState& state)
{
    timeParam    = param (state, "time");
    repeatsParam = param (state, "repeats");
    mixParam     = param (state, "mix");
    ageParam     = param (state, "age");
    wobbleParam  = param (state, "wobble");
}

void TapeEcho::preparePedal (double sr, int)
{
    tape.prepare ((int) std::ceil ((maxTimeMs * 0.001 + maxWobbleSeconds) * sr) + 8);
    loopHighPass.setCutoff (sr, 110.0);
    wow.setRate (sr, 0.45);
    flutter.setRate (sr, 5.8);
    drift.prepare (sr, 0.9);

    // ~150 ms glide when the time knob moves (tape-speed pitch bend).
    glideCoeff = (float) std::exp (-1.0 / (0.15 * sr));

    for (auto* s : { &feedback, &mix, &wobbleDepth, &saturation })
        s->reset (sr, 0.05);

    lastAge = -1.0f;
    resetPedal();
}

void TapeEcho::resetPedal()
{
    tape.reset();
    loopLowPass.reset();
    loopHighPass.reset();
    wow.reset (0.0f);
    flutter.reset (0.5f);
    drift.reset();

    currentDelay = timeParam != nullptr ? timeParam->load() * 0.001f * (float) sampleRate : 0.0f;
    feedback.setCurrentAndTargetValue (repeatsParam != nullptr ? repeatsParam->load() * maxLoopGain : 0.0f);
    mix.setCurrentAndTargetValue (mixParam != nullptr ? mixParam->load() : 0.0f);
    wobbleDepth.setCurrentAndTargetValue (wobbleParam != nullptr ? wobbleParam->load() : 0.0f);
    saturation.setCurrentAndTargetValue (1.0f);
}

void TapeEcho::processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain)
{
    const float targetDelay = timeParam->load() * 0.001f * (float) sampleRate;
    const float age = ageParam->load();

    feedback.setTargetValue (juce::jlimit (0.0f, maxLoopGain, repeatsParam->load() * maxLoopGain));
    mix.setTargetValue (mixParam->load());
    wobbleDepth.setTargetValue (wobbleParam->load());
    saturation.setTargetValue (1.0f + 3.0f * age);

    if (! juce::exactlyEqual (age, lastAge))
    {
        lastAge = age;
        loopLowPass.setCutoff (sampleRate, 9000.0 * std::pow (1500.0 / 9000.0, (double) age));
    }

    const float maxWobble = (float) (maxWobbleSeconds * sampleRate);
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        currentDelay = targetDelay + glideCoeff * (currentDelay - targetDelay);

        const float mod = 0.75f * wow.next() + 0.1f * flutter.next() + 0.3f * drift.next();
        const float delay = currentDelay + maxWobble * (1.0f + juce::jlimit (-1.0f, 1.0f, mod)) * 0.5f * wobbleDepth.getNextValue();

        const float echo = tape.readCubic (delay);
        const float input = 0.5f * (left[i] + right[i]) * sendGain[i];

        // Record head: filter, then saturate. Small-signal loop gain = feedback * |filters| <= 0.97.
        float rec = input + feedback.getNextValue() * echo;
        rec = loopHighPass.process (loopLowPass.process (rec));
        tape.push (dsp::softClip (rec, saturation.getNextValue()));

        const float wet = mix.getNextValue() * echo;
        left[i]  += wet;
        right[i] += wet;
    }
}

} // namespace orion
