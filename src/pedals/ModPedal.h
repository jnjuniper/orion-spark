#pragma once

#include "../Pedal.h"
#include "../DspUtils.h"

namespace orion
{

// Stereo chorus / flanger. Both modes share one modulated delay per channel;
// switching mode glides the delay time and feedback rather than jumping, so it
// doesn't click. Feedback is capped below 1 and soft-clipped in the loop.
class ModPedal final : public Pedal
{
public:
    ModPedal();

protected:
    void addPedalParameters (Layout& layout) override;
    void attachPedal (juce::AudioProcessorValueTreeState& state) override;
    void preparePedal (double sampleRate, int maxBlockSize) override;
    void resetPedal() override;
    void processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain) override;

private:
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* regenParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* flangerParam = nullptr;

    dsp::DelayLine delay[2];
    dsp::Lfo lfo[2];
    float feedbackState[2] = { 0.0f, 0.0f };

    // 0 = chorus, 1 = flanger (smoothed, so mode changes glide).
    juce::SmoothedValue<float> mode, depth, regen, mix, rate;
};

} // namespace orion
