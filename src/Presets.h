#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <utility>
#include <vector>

namespace orion
{

// A complete board state. Parameters that aren't listed are reset to their
// defaults, so loading a preset is always predictable, and presets saved before a
// pedal existed still load once it does. The global input/output gain is never
// part of a preset.
struct Preset
{
    juce::String name;
    juce::String category;
    std::vector<std::pair<juce::String, float>> values;   // paramId -> real (unnormalised) value
};

const std::vector<Preset>& getFactoryPresets();

// Index into getFactoryPresets(), or -1.
int findFactoryPreset (const juce::String& name);

// False for the global input/output gain, which presets leave alone.
bool isPresetParameter (const juce::RangedAudioParameter&);

// The normalised value every preset parameter takes when 'preset' is applied,
// snapped the same way the parameter itself would store it.
std::vector<std::pair<juce::RangedAudioParameter*, float>> resolvePreset (juce::AudioProcessor&, const Preset&);

// Applies a preset to every parameter (must be called on the message thread).
void applyPreset (juce::AudioProcessor&, const Preset&);

// The current board as a preset.
Preset capturePreset (juce::AudioProcessor&, const juce::String& name);

// User presets: one small XML file per preset in a folder, shared by the AU,
// VST3 and standalone builds. Message thread only.
class UserPresetLibrary
{
public:
    // Deleted presets go to the Trash unless 'moveDeletedToTrash' is false (tests).
    explicit UserPresetLibrary (juce::File folder, bool moveDeletedToTrash = true);

    // ~/Library/Application Support/Orion Spark/Presets
    static juce::File getDefaultFolder();

    const juce::File& getFolder() const noexcept { return folder; }

    // Every readable preset in the folder, sorted by name. Unreadable files are skipped.
    std::vector<Preset> scan() const;

    std::optional<Preset> load (const juce::String& name) const;
    bool exists (const juce::String& name) const;
    bool save (const Preset&);          // overwrites an existing preset with the same name
    bool remove (const juce::String& name);
    bool rename (const juce::String& from, const juce::String& to);

    // Trims whitespace; empty if the name can't be used.
    static juce::String cleanName (const juce::String&);

    static constexpr const char* category = "User";
    static constexpr int maxNameLength = 48;

private:
    juce::File fileFor (const juce::String& name) const;
    static std::optional<Preset> read (const juce::File&);

    juce::File folder;
    bool useTrash;
};

} // namespace orion
