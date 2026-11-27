#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Pedal.h"
#include "Tuner.h"
#include <memory>
#include <vector>

class OrionAudioProcessor final : public juce::AudioProcessor
{
public:
    OrionAudioProcessor();
    ~OrionAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Orion Spark"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    // Presets live in Orion Spark's own menu rather than the host's program list:
    // hosts may change programs from the audio thread, and loading a preset
    // touches every parameter, which must happen on the message thread.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Orion Spark"; }
    void changeProgramName (int, const juce::String&) override {}

    int getNumPresets() const;
    int getCurrentPreset() const noexcept { return currentPreset.load(); }

    // Loads a factory preset. Message thread only.
    void loadPreset (int index);

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    const std::vector<std::unique_ptr<orion::Pedal>>& getPedals() const noexcept { return pedals; }

    // ---- Tuner. Deliberately not a parameter and never saved, so a session can't
    // open muted. The editor switches it off when it closes.
    void setTunerActive (bool shouldBeActive) noexcept { tunerActive = shouldBeActive; }   // mutes the output
    bool isTunerActive() const noexcept { return tunerActive.load(); }
    const orion::InputTap& getInputTap() const noexcept { return inputTap; }           // raw input, before any pedal

    static constexpr const char* inputGainId = "in_gain";
    static constexpr const char* outputGainId = "out_gain";

private:
    static std::vector<std::unique_ptr<orion::Pedal>> createPedals();
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Signal chain, in order. Declared before 'state' because the parameter
    // layout is built from it.
    std::vector<std::unique_ptr<orion::Pedal>> pedals;
    juce::AudioProcessorValueTreeState state;

    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;

    orion::InputTap inputTap;
    std::atomic<bool> tunerActive { false };
    // Ramp to silence and back: one before the pedals, so what's played while tuning doesn't
    // ring out of the echo and reverb afterwards, and one at the output for their tails.
    juce::SmoothedValue<float> tunerInputGain { 1.0f }, tunerOutputGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    int maxBlockSize = 512;
    std::atomic<int> currentPreset { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrionAudioProcessor)
};
