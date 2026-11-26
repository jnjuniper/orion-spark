#include "Presets.h"

namespace orion
{

// Tones "inspired by" well-known players: starting points tuned by ear, not
// claims of exact rig replicas. Times are in ms, levels in dB, everything else 0..1.
const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> presets
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

void applyPreset (juce::AudioProcessorValueTreeState& state, const FactoryPreset& preset)
{
    auto& processor = state.processor;

    // Reset everything (except the global in/out gain) to defaults first.
    for (auto* p : processor.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (withId->paramID != "in_gain" && withId->paramID != "out_gain")
                withId->setValueNotifyingHost (withId->getDefaultValue());

    for (const auto& [id, value] : preset.values)
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
}

} // namespace orion
