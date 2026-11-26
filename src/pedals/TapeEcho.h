#pragma once

#include "../Pedal.h"
#include "../DspUtils.h"

namespace orion
{

// Tape echo: every repeat goes back through the "record head" (filters plus
// saturation), so repeats get darker and grittier. Time changes glide like a
// tape motor changing speed. Loop gain is capped below 1, so it can't run away.
class TapeEcho final : public Pedal
{
public:
    TapeEcho();

protected:
    void addPedalParameters (Layout& layout) override;
    void attachPedal (juce::AudioProcessorValueTreeState& state) override;
    void preparePedal (double sampleRate, int maxBlockSize) override;
    void resetPedal() override;
    void processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain) override;
    bool usesTrails() const override { return true; }

private:
    std::atomic<float>* timeParam = nullptr;
    std::atomic<float>* repeatsParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* ageParam = nullptr;
    std::atomic<float>* wobbleParam = nullptr;

    dsp::DelayLine tape;
    dsp::OnePoleLP loopLowPass;
    dsp::OnePoleHP loopHighPass;
    dsp::Lfo wow, flutter;
    dsp::Drift drift;

    float currentDelay = 0.0f;   // samples, glides toward the target
    float glideCoeff = 0.0f;
    float lastAge = -1.0f;

    juce::SmoothedValue<float> feedback, mix, wobbleDepth, saturation;
};

} // namespace orion
