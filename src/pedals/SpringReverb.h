#pragma once

#include "../Pedal.h"
#include "../DspUtils.h"
#include <array>

namespace orion
{

// Spring reverb in the style of a two-spring tank. Each spring is a long chain of
// all-pass filters (dispersion, which gives the "boing"/drip chirp) inside a
// damped feedback delay. The two springs have different lengths, which gives
// stereo width. All loop elements have gain <= 1, so it's stable at every setting.
class SpringReverb final : public Pedal
{
public:
    SpringReverb();

protected:
    void addPedalParameters (Layout& layout) override;
    void attachPedal (juce::AudioProcessorValueTreeState& state) override;
    void preparePedal (double sampleRate, int maxBlockSize) override;
    void resetPedal() override;
    void processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain) override;
    bool usesTrails() const override { return true; }

private:
    static constexpr int numStages = 48;

    struct Spring
    {
        std::array<dsp::AllPass1, numStages> dispersion;
        dsp::DelayLine delay;
        dsp::OnePoleLP damping;
        dsp::Biquad outputTone;
        dsp::Lfo shimmer;
        float loopState = 0.0f;
        int delaySamples = 1;
        float allpassOffset = 0.0f;
    };

    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* dripParam = nullptr;
    std::atomic<float>* mixParam = nullptr;

    std::array<Spring, 2> springs;
    dsp::Biquad inputHighPass, inputLowPass;

    juce::SmoothedValue<float> mix, drip;
    float lastTone = -1.0f;
};

} // namespace orion
