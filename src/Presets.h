#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <utility>
#include <vector>

namespace orion
{

// A factory preset. Parameters that aren't listed are reset to their defaults,
// so every preset is a complete, predictable board state.
struct FactoryPreset
{
    juce::String name;
    juce::String category;
    std::vector<std::pair<juce::String, float>> values;   // paramId -> real (unnormalised) value
};

const std::vector<FactoryPreset>& getFactoryPresets();

// Applies a preset to every parameter (must be called on the message thread).
void applyPreset (juce::AudioProcessorValueTreeState& state, const FactoryPreset& preset);

} // namespace orion
