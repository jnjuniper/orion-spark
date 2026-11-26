#pragma once

#include "../Pedal.h"
#include "../DspUtils.h"
#include <juce_dsp/juce_dsp.h>

namespace orion
{

// British high-gain amp in a box (JCM800-ish): bright input, three cascaded
// asymmetric tube-like gain stages at 4x oversampling with interstage
// coupling/Miller filtering, a bass/mid/treble tone stack, soft power-amp
// saturation with presence and resonance, and a 4x12 cabinet simulation.
class BritStack final : public Pedal
{
public:
    BritStack();
    int getLatencySamples() const override { return latency; }

protected:
    void addPedalParameters (Layout& layout) override;
    void attachPedal (juce::AudioProcessorValueTreeState& state) override;
    void preparePedal (double sampleRate, int maxBlockSize) override;
    void resetPedal() override;
    void processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain) override;

private:
    void updateFilters (bool force);

    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* bassParam = nullptr;
    std::atomic<float>* midParam = nullptr;
    std::atomic<float>* trebleParam = nullptr;
    std::atomic<float>* presenceParam = nullptr;
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* cabParam = nullptr;

    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    struct Channel
    {
        // Base rate, before the preamp.
        dsp::Biquad inputHighPass, bright;
        // Oversampled preamp: coupling high-passes and "Miller" low-passes.
        dsp::OnePoleHP couple1, couple2, couple3;
        dsp::OnePoleLP miller1, miller2, miller3;
        // Base rate, after the preamp.
        dsp::Biquad bass, mid, treble, presence, resonance;
        dsp::OnePoleHP dcBlock;
        // Cabinet.
        dsp::Biquad cabHighPass, cabThump, cabBox, cabPresence, cabLowPass1, cabLowPass2, cabDip;

        void reset();
    };

    Channel channels[2];

    juce::SmoothedValue<float> stage1Gain, stage2Gain, stage3Gain;   // oversampled rate
    juce::SmoothedValue<float> makeup, cabMix;                        // base rate

    float lastGain = -1.0f, lastBass = -1.0f, lastMid = -1.0f, lastTreble = -1.0f, lastPresence = -1.0f;
    int latency = 0;
};

} // namespace orion
