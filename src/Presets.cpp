#include "Presets.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace orion
{

// Tones "inspired by" well-known players: starting points tuned by ear, not
// claims of exact rig replicas. Times are in ms, levels in dB, everything else 0..1.
const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets
    {
        { "Init", "Basics", {} },

        { "Clean Tape Space", "Basics",
          { { "gate_on", 1 }, { "pre_on", 1 }, { "pre_drive", 0.2f }, { "pre_wobble", 0.1f },
            { "echo_on", 1 }, { "echo_mix", 0.2f }, { "spring_on", 1 }, { "spring_mix", 0.3f } } },

        // --- John Sykes, '87 Whitesnake: boosted British stack, singing sustain, wide delay.
        { "Sykes '87 Lead", "John Sykes",
          { { "gate_on", 1 }, { "gate_threshold", -58 }, { "gate_release", 140 },
            { "od_on", 1 }, { "od_drive", 0.15f }, { "od_tone", 0.6f }, { "od_level", 6 },
            { "amp_on", 1 }, { "amp_gain", 0.78f }, { "amp_bass", 0.5f }, { "amp_mid", 0.68f },
            { "amp_treble", 0.6f }, { "amp_presence", 0.65f }, { "amp_master", 0 }, { "amp_cab", 1 },
            { "pre_on", 0 },
            { "mod_on", 1 }, { "mod_flanger", 0 }, { "mod_rate", 0.4f }, { "mod_depth", 0.3f },
            { "mod_regen", 0.1f }, { "mod_mix", 0.3f },
            { "echo_on", 1 }, { "echo_time", 420 }, { "echo_repeats", 0.38f }, { "echo_mix", 0.24f },
            { "echo_age", 0.15f }, { "echo_wobble", 0.05f },
            { "spring_on", 1 }, { "spring_decay", 0.5f }, { "spring_tone", 0.7f }, { "spring_drip", 0.2f },
            { "spring_mix", 0.12f } } },

        { "Sykes '87 Rhythm", "John Sykes",
          { { "gate_on", 1 }, { "gate_threshold", -55 }, { "gate_release", 60 },
            { "od_on", 1 }, { "od_drive", 0.05f }, { "od_tone", 0.55f }, { "od_level", 4 },
            { "amp_on", 1 }, { "amp_gain", 0.66f }, { "amp_bass", 0.55f }, { "amp_mid", 0.55f },
            { "amp_treble", 0.6f }, { "amp_presence", 0.6f }, { "amp_master", 0 }, { "amp_cab", 1 },
            { "pre_on", 0 }, { "mod_on", 0 }, { "echo_on", 0 },
            { "spring_on", 1 }, { "spring_decay", 0.35f }, { "spring_mix", 0.08f } } },

        // --- Mk.gee: cassette-preamp breakup, warble, slow flange, washed-out space.
        { "Dream Tape", "Mk.gee",
          { { "gate_on", 0 }, { "od_on", 0 }, { "amp_on", 0 },
            { "pre_on", 1 }, { "pre_drive", 0.55f }, { "pre_tone", 0.4f }, { "pre_wobble", 0.45f },
            { "pre_hiss", 0.25f }, { "pre_level", 0 },
            { "mod_on", 1 }, { "mod_flanger", 1 }, { "mod_rate", 0.15f }, { "mod_depth", 0.6f },
            { "mod_regen", 0.35f }, { "mod_mix", 0.35f },
            { "echo_on", 1 }, { "echo_time", 140 }, { "echo_repeats", 0.25f }, { "echo_mix", 0.18f },
            { "echo_age", 0.75f }, { "echo_wobble", 0.6f },
            { "spring_on", 1 }, { "spring_decay", 0.65f }, { "spring_tone", 0.35f }, { "spring_drip", 0.6f },
            { "spring_mix", 0.38f } } },

        { "Cassette Crunch", "Mk.gee",
          { { "gate_on", 0 }, { "od_on", 0 }, { "amp_on", 0 },
            { "pre_on", 1 }, { "pre_drive", 0.82f }, { "pre_tone", 0.35f }, { "pre_wobble", 0.25f },
            { "pre_hiss", 0.35f }, { "pre_level", -2 },
            { "mod_on", 1 }, { "mod_flanger", 0 }, { "mod_rate", 0.3f }, { "mod_depth", 0.4f },
            { "mod_regen", 0.1f }, { "mod_mix", 0.25f },
            { "echo_on", 0 },
            { "spring_on", 1 }, { "spring_decay", 0.4f }, { "spring_tone", 0.3f }, { "spring_mix", 0.25f } } },

        // --- 70s classic rock: Plexi-style crunch, tape echo, a little spring.
        { "Plexi Crunch", "70s Rock",
          { { "gate_on", 0 }, { "od_on", 0 },
            { "amp_on", 1 }, { "amp_gain", 0.4f }, { "amp_bass", 0.55f }, { "amp_mid", 0.7f },
            { "amp_treble", 0.6f }, { "amp_presence", 0.5f }, { "amp_master", 0 }, { "amp_cab", 1 },
            { "pre_on", 0 }, { "mod_on", 0 },
            { "echo_on", 1 }, { "echo_time", 300 }, { "echo_repeats", 0.2f }, { "echo_mix", 0.15f },
            { "echo_age", 0.5f }, { "echo_wobble", 0.2f },
            { "spring_on", 1 }, { "spring_decay", 0.4f }, { "spring_mix", 0.15f } } },

        { "Echo Lead", "70s Rock",
          { { "gate_on", 0 },
            { "od_on", 1 }, { "od_drive", 0.45f }, { "od_tone", 0.5f }, { "od_level", 2 },
            { "amp_on", 1 }, { "amp_gain", 0.5f }, { "amp_bass", 0.5f }, { "amp_mid", 0.6f },
            { "amp_treble", 0.55f }, { "amp_presence", 0.5f }, { "amp_master", 0 }, { "amp_cab", 1 },
            { "pre_on", 0 },
            { "mod_on", 1 }, { "mod_flanger", 0 }, { "mod_rate", 0.5f }, { "mod_depth", 0.25f },
            { "mod_regen", 0.1f }, { "mod_mix", 0.2f },
            { "echo_on", 1 }, { "echo_time", 480 }, { "echo_repeats", 0.45f }, { "echo_mix", 0.3f },
            { "echo_age", 0.45f }, { "echo_wobble", 0.25f },
            { "spring_on", 1 }, { "spring_decay", 0.55f }, { "spring_mix", 0.22f } } },

        // --- Metal rhythm: tight boost into high gain, scooped, gated, dry.
        { "Thrash '88 Rhythm", "Metal",
          { { "gate_on", 1 }, { "gate_threshold", -48 }, { "gate_release", 35 },
            { "od_on", 1 }, { "od_drive", 0.0f }, { "od_tone", 0.7f }, { "od_level", 9 },
            { "amp_on", 1 }, { "amp_gain", 0.86f }, { "amp_bass", 0.62f }, { "amp_mid", 0.2f },
            { "amp_treble", 0.66f }, { "amp_presence", 0.6f }, { "amp_master", 0 }, { "amp_cab", 1 },
            { "pre_on", 0 }, { "mod_on", 0 }, { "echo_on", 0 }, { "spring_on", 0 } } },

        { "Black '91 Rhythm", "Metal",
          { { "gate_on", 1 }, { "gate_threshold", -50 }, { "gate_release", 50 },
            { "od_on", 1 }, { "od_drive", 0.05f }, { "od_tone", 0.6f }, { "od_level", 6 },
            { "amp_on", 1 }, { "amp_gain", 0.8f }, { "amp_bass", 0.66f }, { "amp_mid", 0.4f },
            { "amp_treble", 0.55f }, { "amp_presence", 0.5f }, { "amp_master", 0 }, { "amp_cab", 1 },
            { "pre_on", 0 }, { "mod_on", 0 }, { "echo_on", 0 }, { "spring_on", 0 } } },
    };

    return presets;
}


int findFactoryPreset (const juce::String& name)
{
    const auto& presets = getFactoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == name)
            return (int) i;
    return -1;
}

bool isPresetParameter (const juce::RangedAudioParameter& param)
{
    return param.paramID != "in_gain" && param.paramID != "out_gain";
}

std::vector<std::pair<juce::RangedAudioParameter*, float>> resolvePreset (juce::AudioProcessor& processor, const Preset& preset)
{
    std::vector<std::pair<juce::RangedAudioParameter*, float>> resolved;

    for (auto* p : processor.getParameters())
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p); param != nullptr && isPresetParameter (*param))
            resolved.emplace_back (param, param->getDefaultValue());

    // Unknown IDs (e.g. from a newer version) are ignored.
    for (const auto& [id, value] : preset.values)
        for (auto& [param, normalised] : resolved)
            if (param->paramID == id && std::isfinite (value))
                normalised = param->convertTo0to1 (value);

    // Snap like the parameter does when it stores a value, so the result can be
    // compared with getValue() afterwards.
    for (auto& [param, normalised] : resolved)
        normalised = param->convertTo0to1 (param->convertFrom0to1 (normalised));

    return resolved;
}

void applyPreset (juce::AudioProcessor& processor, const Preset& preset)
{
    for (const auto& [param, normalised] : resolvePreset (processor, preset))
        param->setValueNotifyingHost (normalised);
}

Preset capturePreset (juce::AudioProcessor& processor, const juce::String& name)
{
    Preset preset { name, UserPresetLibrary::category, {} };

    for (auto* p : processor.getParameters())
        if (auto* param = dynamic_cast<juce::RangedAudioParameter*> (p); param != nullptr && isPresetParameter (*param))
            preset.values.emplace_back (param->paramID, param->convertFrom0to1 (param->getValue()));

    return preset;
}

// ---------------------------------------------------------------------------
namespace
{
    constexpr const char* presetExtension = ".orionpreset";
    constexpr const char* presetTag = "OrionPreset";
    constexpr int presetFormatVersion = 1;
}

UserPresetLibrary::UserPresetLibrary (juce::File presetFolder, bool moveDeletedToTrash)
    : folder (std::move (presetFolder)), useTrash (moveDeletedToTrash) {}

juce::File UserPresetLibrary::getDefaultFolder()
{
    // userApplicationDataDirectory is ~/Library on macOS.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support")
        .getChildFile ("Orion Spark")
        .getChildFile ("Presets");
}

juce::String UserPresetLibrary::cleanName (const juce::String& name)
{
    // The preset's name is its file name, so drop what a file name can't hold.
    juce::String cleaned;
    for (auto p = name.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c >= 32 && c != 127 && juce::String ("\"#@,;:<>*^|?\\/").indexOfChar (c) < 0)
            cleaned += c;
    }

    cleaned = cleaned.trim().trimCharactersAtStart (".").trim();   // no hidden files, no ".."
    if (cleaned.length() > maxNameLength)
        cleaned = cleaned.substring (0, maxNameLength).trimEnd();
    return cleaned;
}

juce::File UserPresetLibrary::fileFor (const juce::String& name) const
{
    const auto cleaned = cleanName (name);
    return cleaned.isEmpty() ? juce::File() : folder.getChildFile (cleaned + presetExtension);
}

std::optional<Preset> UserPresetLibrary::read (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (presetTag))
        return std::nullopt;

    // The file name is the preset's name, so renaming the file in Finder renames the preset.
    Preset preset { file.getFileNameWithoutExtension(), category, {} };

    for (auto* param : xml->getChildWithTagNameIterator ("Param"))
    {
        const auto id = param->getStringAttribute ("id");
        const auto value = (float) param->getDoubleAttribute ("value", std::numeric_limits<double>::quiet_NaN());
        if (id.isNotEmpty() && std::isfinite (value))
            preset.values.emplace_back (id, value);
    }

    return preset;
}

std::vector<Preset> UserPresetLibrary::scan() const
{
    std::vector<Preset> presets;

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension))
        if (const auto stem = file.getFileNameWithoutExtension(); stem != cleanName (stem))
            continue;   // e.g. "a, b" added in Finder: load/rename/delete would resolve it to a different name
        else if (auto preset = read (file))
            presets.push_back (std::move (*preset));

    std::sort (presets.begin(), presets.end(),
               [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    return presets;
}

std::optional<Preset> UserPresetLibrary::load (const juce::String& name) const
{
    const auto file = fileFor (name);
    return file.existsAsFile() ? read (file) : std::nullopt;
}

bool UserPresetLibrary::exists (const juce::String& name) const
{
    return fileFor (name).existsAsFile();
}

bool UserPresetLibrary::save (const Preset& preset)
{
    const auto file = fileFor (preset.name);
    if (file == juce::File() || ! folder.createDirectory().wasOk())
        return false;

    juce::XmlElement xml (presetTag);
    xml.setAttribute ("version", presetFormatVersion);
    for (const auto& [id, value] : preset.values)
    {
        auto* param = xml.createNewChildElement ("Param");
        param->setAttribute ("id", id);
        param->setAttribute ("value", (double) value);
    }

    return xml.writeTo (file);   // writes a temporary file first, so a failed save never corrupts the old one
}

bool UserPresetLibrary::remove (const juce::String& name)
{
    const auto file = fileFor (name);
    return file.existsAsFile() && (useTrash ? file.moveToTrash() : file.deleteFile());
}

bool UserPresetLibrary::rename (const juce::String& from, const juce::String& to)
{
    const auto source = fileFor (from), target = fileFor (to);
    if (! source.existsAsFile() || target == juce::File())
        return false;

    // Refuse to replace a different preset (a case-only rename is the same file on macOS).
    if (target.existsAsFile() && target != source)
        return false;

    return source.moveFileTo (target);
}

} // namespace orion
