#pragma once

#include "../Pedal.h"

namespace orion
{

// Stereo-linked noise gate with hysteresis and hold, so it doesn't chatter on
// decaying notes.
class NoiseGate final : public Pedal
{
public:
    NoiseGate();

protected:
    void addPedalParameters (Layout& layout) override;
    void attachPedal (juce::AudioProcessorValueTreeState& state) override;
    void preparePedal (double sampleRate, int maxBlockSize) override;
    void resetPedal() override;
    void processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain) override;

private:
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;

    float envelope = 0.0f;
    float gain = 0.0f;
    bool open = false;
    int holdCounter = 0;

    float envReleaseCoeff = 0.0f;
    float attackCoeff = 0.0f;
    int holdSamples = 0;
};

} // namespace orion
