#include "BritStack.h"

namespace orion
{

namespace
{
    constexpr int oversamplingFactor = 4;
    constexpr float referenceLevel = 0.18f;   // ~ -15 dBFS peak guitar

    // Asymmetric biases per stage -> even harmonics, like real triode stages.
    constexpr float bias1 = 0.25f, bias2 = -0.2f, bias3 = 0.1f;

    // Calibration: makes the amp at default settings come out at roughly the
    // input's loudness (measured with a -18 dBFS 196 Hz sine in OrionTests).
    constexpr float outputCalibration = 0.93f;

    inline float tube (float x, float bias) noexcept { return std::tanh (x + bias) - std::tanh (bias); }
    inline float powerAmp (float x) noexcept { return std::tanh (1.2f * x) / 1.2f; }

    struct StageGains { float g1, g2, g3; };

    // Gain 0 = edge of breakup, 1 = modern high gain (14 dB .. 60 dB total).
    StageGains stageGainsFor (float gain)
    {
        const float totalDb = 14.0f + 46.0f * gain;
        return { dsp::dbToGain (0.45f * totalDb), dsp::dbToGain (0.40f * totalDb), dsp::dbToGain (0.15f * totalDb) };
    }

    // Static estimate of the preamp's output level for a reference input, used
    // to keep loudness roughly constant across the Gain knob.
    float makeupFor (const StageGains& g)
    {
        const auto stage = [] (float a, float gain, float bias)
        {
            return 0.5f * (std::abs (tube (gain * a, bias)) + std::abs (tube (-gain * a, bias)));
        };

        float a = referenceLevel;
        a = stage (a, g.g1, bias1);
        a = stage (a, g.g2, bias2);
        a = stage (a, g.g3, bias3);
        a = powerAmp (a);
        return outputCalibration * referenceLevel / juce::jmax (1.0e-6f, a);
    }
}

void BritStack::Channel::reset()
{
    for (auto* b : { &inputHighPass, &bright, &bass, &mid, &treble, &presence, &resonance,
                     &cabHighPass, &cabThump, &cabBox, &cabPresence, &cabLowPass1, &cabLowPass2, &cabDip })
        b->reset();

    for (auto* f : { &couple1, &couple2, &couple3, &dcBlock })
        f->reset();

    for (auto* f : { &miller1, &miller2, &miller3 })
        f->reset();
}

BritStack::BritStack()
    : Pedal ("amp", "Brit Stack", "Amp + 4x12 cab", "STACK", juce::Colour (0xff5a1f1f), false)
{
}

void BritStack::addPedalParameters (Layout& layout)
{
    const auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };

    addFloat (layout, "gain", "Gain", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "bass", "Bass", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "mid", "Mid", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "treble", "Treble", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "presence", "Presence", { 0.0f, 1.0f }, 0.5f, percent);
    addFloat (layout, "master", "Master", { -24.0f, 12.0f, 0.1f }, 0.0f,
              [] (float v) { return juce::String (v, 1) + " dB"; });
    addSwitch (layout, "cab", "Cab", true);
}

void BritStack::attachPedal (juce::AudioProcessorValueTreeState& state)
{
    gainParam     = param (state, "gain");
    bassParam     = param (state, "bass");
    midParam      = param (state, "mid");
    trebleParam   = param (state, "treble");
    presenceParam = param (state, "presence");
    masterParam   = param (state, "master");
    cabParam      = param (state, "cab");
}

void BritStack::preparePedal (double sr, int maxBlockSize)
{
    oversampling.initProcessing ((size_t) maxBlockSize);
    latency = (int) std::round (oversampling.getLatencyInSamples());

    const double osr = sr * oversamplingFactor;

    for (auto& c : channels)
    {
        c.inputHighPass.setHighPass (sr, 30.0);

        c.couple1.setCutoff (osr, 100.0);
        c.couple2.setCutoff (osr, 200.0);
        c.couple3.setCutoff (osr, 20.0);
        c.miller1.setCutoff (osr, 9000.0);
        c.miller2.setCutoff (osr, 7500.0);
        c.miller3.setCutoff (osr, 6500.0);

        c.resonance.setPeak (sr, 90.0, 1.0, 3.0);
        c.dcBlock.setCutoff (sr, 10.0);

        c.cabHighPass.setHighPass (sr, 75.0, 0.7);
        c.cabThump.setPeak (sr, 110.0, 1.2, 4.0);
        c.cabBox.setPeak (sr, 400.0, 1.0, -5.0);
        c.cabPresence.setPeak (sr, 2500.0, 1.5, 4.0);
        c.cabLowPass1.setLowPass (sr, 4900.0, 0.54);  // 4th-order Butterworth pair
        c.cabLowPass2.setLowPass (sr, 4900.0, 1.31);
        c.cabDip.setPeak (sr, 7000.0, 2.0, -8.0);
    }

    for (auto* s : { &stage1Gain, &stage2Gain, &stage3Gain })
        s->reset (osr, 0.03);
    makeup.reset (sr, 0.03);
    cabMix.reset (sr, 0.03);

    resetPedal();
}

void BritStack::resetPedal()
{
    oversampling.reset();
    for (auto& c : channels)
        c.reset();

    const auto g = stageGainsFor (gainParam != nullptr ? gainParam->load() : 0.5f);
    stage1Gain.setCurrentAndTargetValue (g.g1);
    stage2Gain.setCurrentAndTargetValue (g.g2);
    stage3Gain.setCurrentAndTargetValue (g.g3);
    makeup.setCurrentAndTargetValue (makeupFor (g) * dsp::dbToGain (masterParam != nullptr ? masterParam->load() : 0.0f));
    cabMix.setCurrentAndTargetValue (cabParam == nullptr || cabParam->load() > 0.5f ? 1.0f : 0.0f);

    lastGain = lastBass = lastMid = lastTreble = lastPresence = -1.0f;
    updateFilters (true);
}

void BritStack::updateFilters (bool force)
{
    if (gainParam == nullptr)
        return;

    const float gain = gainParam->load(), bassV = bassParam->load(), midV = midParam->load(),
                trebleV = trebleParam->load(), presenceV = presenceParam->load();

    const auto changed = [force] (float v, float& last)
    {
        if (! force && juce::exactlyEqual (v, last))
            return false;
        last = v;
        return true;
    };

    // Bright cap: matters most at low gain settings.
    if (changed (gain, lastGain))
        for (auto& c : channels)
            c.bright.setHighShelf (sampleRate, 3000.0, 6.0 - 4.0 * gain);

    if (changed (bassV, lastBass))
        for (auto& c : channels)
            c.bass.setLowShelf (sampleRate, 120.0, -12.0 + 20.0 * bassV);

    if (changed (midV, lastMid))
        for (auto& c : channels)
            c.mid.setPeak (sampleRate, 650.0, 0.7, -12.0 + 20.0 * midV);

    if (changed (trebleV, lastTreble))
        for (auto& c : channels)
            c.treble.setHighShelf (sampleRate, 2500.0, -12.0 + 20.0 * trebleV);

    if (changed (presenceV, lastPresence))
        for (auto& c : channels)
            c.presence.setHighShelf (sampleRate, 3500.0, 8.0 * presenceV);
}

void BritStack::processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float*)
{
    updateFilters (false);

    const auto g = stageGainsFor (gainParam->load());
    stage1Gain.setTargetValue (g.g1);
    stage2Gain.setTargetValue (g.g2);
    stage3Gain.setTargetValue (g.g3);
    makeup.setTargetValue (makeupFor (g) * dsp::dbToGain (masterParam->load()));
    cabMix.setTargetValue (cabParam->load() > 0.5f ? 1.0f : 0.0f);

    float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    // Input conditioning at base rate.
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& c = channels[ch];
        for (int i = 0; i < numSamples; ++i)
            io[ch][i] = c.bright.process (c.inputHighPass.process (io[ch][i]));
    }

    // Preamp at 4x.
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) numSamples);
        auto up = oversampling.processSamplesUp (block);
        float* d[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };

        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float g1 = stage1Gain.getNextValue(), g2 = stage2Gain.getNextValue(), g3 = stage3Gain.getNextValue();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = channels[ch];
                float x = d[ch][i];
                x = c.miller1.process (c.couple1.process (tube (x * g1, bias1)));
                x = c.miller2.process (c.couple2.process (tube (x * g2, bias2)));
                x = c.miller3.process (c.couple3.process (tube (x * g3, bias3)));
                d[ch][i] = x;
            }
        }

        oversampling.processSamplesDown (block);
    }

    // Tone stack, power amp, cab.
    for (int i = 0; i < numSamples; ++i)
    {
        const float mk = makeup.getNextValue();
        const float cab = cabMix.getNextValue();

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = channels[ch];
            float x = c.dcBlock.process (io[ch][i]);
            x = c.treble.process (c.mid.process (c.bass.process (x)));
            x = powerAmp (x);
            x = c.resonance.process (c.presence.process (x));

            float y = c.cabHighPass.process (x);
            y = c.cabBox.process (c.cabThump.process (y));
            y = c.cabPresence.process (y);
            y = c.cabDip.process (c.cabLowPass2.process (c.cabLowPass1.process (y)));

            io[ch][i] = (x + cab * (y - x)) * mk;
        }
    }
}

} // namespace orion
