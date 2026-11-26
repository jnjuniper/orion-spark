#include "FourTrackPre.h"

namespace orion
{

namespace
{
    constexpr float shaperBias = 0.25f;            // asymmetry -> even harmonics
    constexpr float maxDriveDb = 36.0f;
    constexpr float referenceLevel = 0.18f;        // ~ -15 dBFS peak, a typical guitar DI level

    // Auto-gain: the gain that makes a reference-level signal come out at the same
    // level it went in, whatever the drive. Turning Drive changes the character,
    // not the volume; the Level knob sets the volume.
    float makeupFor (float driveGain)
    {
        const float offset = std::tanh (shaperBias);
        const float x = driveGain * referenceLevel;
        const float out = 0.5f * (std::abs (std::tanh (x + shaperBias) - offset) + std::abs (std::tanh (-x + shaperBias) - offset));
        return referenceLevel / juce::jmax (1.0e-6f, out);
    }
    constexpr double baseDelaySeconds = 0.003;     // centre of the wobble delay
    constexpr double maxWobbleSeconds = 0.0015;
}

FourTrackPre::FourTrackPre()
    : Pedal ("pre", "4-Track Pre", "Cassette preamp", "4-TRK", juce::Colour (0xff2c5a58))
{
}

void FourTrackPre::addPedalParameters (Layout& layout)
{
    const auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    addFloat (layout, "drive", "Drive", { 0.0f, 1.0f }, 0.4f, percent);
    addFloat (layout, "tone", "Tone", { 0.0f, 1.0f }, 0.6f, percent);
    addFloat (layout, "wobble", "Wobble", { 0.0f, 1.0f }, 0.15f, percent);
    addFloat (layout, "hiss", "Hiss", { 0.0f, 1.0f }, 0.0f, percent);
    addFloat (layout, "level", "Level", { -24.0f, 12.0f, 0.1f }, 0.0f,
              [] (float v) { return juce::String (v, 1) + " dB"; });
}

void FourTrackPre::attachPedal (juce::AudioProcessorValueTreeState& state)
{
    driveParam  = param (state, "drive");
    toneParam   = param (state, "tone");
    wobbleParam = param (state, "wobble");
    hissParam   = param (state, "hiss");
    levelParam  = param (state, "level");
}

void FourTrackPre::preparePedal (double sr, int maxBlockSize)
{
    oversampling.initProcessing ((size_t) maxBlockSize);

    for (int ch = 0; ch < 2; ++ch)
    {
        inputHighPass[ch].setHighPass (sr, 60.0);
        inputBump[ch].setPeak (sr, 220.0, 0.8, 2.5);
        hissFilter[ch].setHighPass (sr, 2500.0, 0.6);
        dcBlock[ch].setCutoff (sr, 12.0);
        wobbleDelay[ch].prepare ((int) std::ceil ((baseDelaySeconds + maxWobbleSeconds) * sr) + 8);
    }

    wow.setRate (sr, 0.55);
    flutter.setRate (sr, 6.5);
    drift.prepare (sr, 1.3);

    baseDelaySamples = (int) std::round (baseDelaySeconds * sr);
    latency = (int) std::round (oversampling.getLatencyInSamples()) + baseDelaySamples;

    for (auto* s : { &driveGain, &makeupGain, &wobbleDepth, &hissGain })
        s->reset (sr, 0.03);

    lastTone = -1.0f;
    resetPedal();
}

void FourTrackPre::resetPedal()
{
    oversampling.reset();

    for (int ch = 0; ch < 2; ++ch)
    {
        inputHighPass[ch].reset();
        inputBump[ch].reset();
        toneLowPass[ch].reset();
        hissFilter[ch].reset();
        dcBlock[ch].reset();
        wobbleDelay[ch].reset();
    }

    wow.reset (0.0f);
    flutter.reset (0.3f);
    drift.reset();

    const float driveDb = driveParam != nullptr ? driveParam->load() * maxDriveDb : 0.0f;
    const float levelDb = levelParam != nullptr ? levelParam->load() : 0.0f;
    driveGain.setCurrentAndTargetValue (dsp::dbToGain (driveDb));
    makeupGain.setCurrentAndTargetValue (dsp::dbToGain (levelDb) * makeupFor (dsp::dbToGain (driveDb)));
    wobbleDepth.setCurrentAndTargetValue (wobbleParam != nullptr ? wobbleParam->load() : 0.0f);
    hissGain.setCurrentAndTargetValue (0.0f);
}

void FourTrackPre::processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float*)
{
    const float driveDb = driveParam->load() * maxDriveDb;
    const float hiss = hissParam->load();

    driveGain.setTargetValue (dsp::dbToGain (driveDb));
    makeupGain.setTargetValue (dsp::dbToGain (levelParam->load()) * makeupFor (dsp::dbToGain (driveDb)));
    wobbleDepth.setTargetValue (wobbleParam->load());
    hissGain.setTargetValue (hiss * hiss * dsp::dbToGain (-48.0f));

    if (const float tone = toneParam->load(); ! juce::exactlyEqual (tone, lastTone))
    {
        lastTone = tone;
        const double cutoff = 1800.0 * std::pow (10.0, (double) tone);
        for (auto& f : toneLowPass)
            f.setLowPass (sampleRate, cutoff);
    }

    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);

    // 1) Band-limit like a cassette channel and apply drive.
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = driveGain.getNextValue();
        left[i]  = inputBump[0].process (inputHighPass[0].process (left[i])) * g;
        right[i] = inputBump[1].process (inputHighPass[1].process (right[i])) * g;
    }

    // 2) Asymmetric breakup at 4x sample rate to keep aliasing down.
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
        auto up = oversampling.processSamplesUp (block);
        const float biasOffset = std::tanh (shaperBias);

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = std::tanh (d[i] + shaperBias) - biasOffset;
        }

        oversampling.processSamplesDown (block);
    }

    // 3) DC block, tone, makeup, tape wobble and hiss.
    const float baseDelay = (float) baseDelaySamples;
    const float maxWobble = (float) (maxWobbleSeconds * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        const float makeup = makeupGain.getNextValue();
        const float mod = 0.7f * wow.next() + 0.08f * flutter.next() + 0.3f * drift.next();
        const float delay = baseDelay + juce::jlimit (-1.0f, 1.0f, mod) * wobbleDepth.getNextValue() * maxWobble;
        const float h = hissGain.getNextValue();

        float* channels[2] = { left, right };
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = toneLowPass[ch].process (dcBlock[ch].process (channels[ch][i])) * makeup;
            wobbleDelay[ch].push (x);
            x = wobbleDelay[ch].readCubic (delay);

            if (h > 0.0f)
                x += h * hissFilter[ch].process (noise.nextFloat() * 2.0f - 1.0f);

            channels[ch][i] = x;
        }
    }
}

} // namespace orion
