#pragma once

#include "../Pedal.h"
#include "../DspUtils.h"
#include <juce_dsp/juce_dsp.h>

namespace orion
{

// Cassette 4-track style preamp: band-limited, asymmetric (even-harmonic)
// breakup at 4x oversampling, a tone control, and optional tape wobble and hiss.
class FourTrackPre final : public Pedal
{
public:
    FourTrackPre();
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
    std::atomic<float>* wobbleParam = nullptr;
    std::atomic<float>* hissParam = nullptr;
    std::atomic<float>* levelParam = nullptr;

    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    dsp::Biquad inputHighPass[2], inputBump[2], toneLowPass[2], hissFilter[2];
    dsp::OnePoleHP dcBlock[2];
    dsp::DelayLine wobbleDelay[2];
    dsp::Lfo wow, flutter;
    dsp::Drift drift;
    juce::Random noise { 0x424 };

    juce::SmoothedValue<float> driveGain, makeupGain, wobbleDepth, hissGain;
    float lastTone = -1.0f;

    int baseDelaySamples = 0;
    int latency = 0;
};

} // namespace orion
