#pragma once

#include "../Pedal.h"
#include "../DspUtils.h"
#include <juce_dsp/juce_dsp.h>

namespace orion
{

// Tube-Screamer-style overdrive: the full-band dry signal plus a soft-clipped,
// high-passed (720 Hz) copy, giving the classic mid hump. Clipping runs at 4x
// oversampling.
class Overdrive final : public Pedal
{
public:
    Overdrive();
    int getLatencySamples() const override { return latency; }

protected:
    void addPedalParameters (Layout& layout) override;
    void attachPedal (juce::AudioProcessorValueTreeState& state) override;
    void preparePedal (double sampleRate, int maxBlockSize) override;
    void resetPedal() override;
    void processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain) override;

private:
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* levelParam = nullptr;

    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    dsp::Biquad inputHighPass[2], toneLowPass[2];
    dsp::OnePoleHP clipHighPass[2];   // runs at the oversampled rate

    juce::SmoothedValue<float> clipGain, outputGain;
    float lastTone = -1.0f;
    int latency = 0;
};

} // namespace orion
