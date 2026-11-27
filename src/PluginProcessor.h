#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Pedal.h"
#include "Presets.h"
#include <memory>
#include <vector>

class OrionAudioProcessor final : public juce::AudioProcessor
{
public:
    explicit OrionAudioProcessor (orion::UserPresetLibrary userPresetLibrary = orion::UserPresetLibrary (orion::UserPresetLibrary::getDefaultFolder()));
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

    // ---- Presets. Message thread only, except where noted.
    struct PresetRef
    {
        bool isUser = false;
        juce::String name;
    };

    PresetRef getCurrentPreset() const;                       // any thread
    juce::uint32 getPresetChangeCount() const noexcept { return presetChangeCount.load(); }   // any thread; bumps when
                                                              // the current preset or the user preset library changes

    // True when the knobs differ from the current preset as saved (or it no longer exists).
    bool isPresetModified();

    void loadFactoryPreset (int index);
    bool loadUserPreset (const juce::String& name);

    // Each of these returns false if the file operation failed.
    bool saveUserPreset (const juce::String& name);          // creates or overwrites, then makes it current
    bool renameUserPreset (const juce::String& from, const juce::String& to);
    bool deleteUserPreset (const juce::String& name);       // moves the file to the Trash

    const orion::UserPresetLibrary& getUserPresets() const noexcept { return userPresets; }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    const std::vector<std::unique_ptr<orion::Pedal>>& getPedals() const noexcept { return pedals; }

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
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    int maxBlockSize = 512;

    void setCurrentPreset (PresetRef);
    void rebuildPresetBaseline();

    orion::UserPresetLibrary userPresets;
    mutable juce::SpinLock presetLock;
    PresetRef currentPreset;                                  // guarded by presetLock
    std::atomic<juce::uint32> presetChangeCount { 0 };

    // The current preset's resolved values, to tell whether the knobs were changed.
    // Message thread only; rebuilt lazily when the preset changes from elsewhere.
    std::vector<std::pair<juce::RangedAudioParameter*, float>> presetBaseline;
    bool presetBaselineKnown = false;
    std::atomic<bool> presetBaselineStale { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrionAudioProcessor)
};
