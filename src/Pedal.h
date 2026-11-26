#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace orion
{

// A knob shown on the pedal's face in the editor.
struct KnobInfo
{
    juce::String paramId;
    juce::String label;
};

// Base class for every pedal in the chain.
//
// - Parameter IDs are namespaced "<pedalId>_<name>" and must never change once
//   released (saved Ableton sets and presets refer to them).
// - Every pedal gets an "<pedalId>_on" footswitch. The base class turns it into a
//   click-free crossfade; pedals with latency keep running while bypassed so the
//   reported latency never changes, and "trails" pedals (echo, reverb) keep their
//   tails ringing out after being switched off.
// - Audio is always processed as stereo (2 channels).
class Pedal
{
public:
    Pedal (juce::String pedalId, juce::String displayName, juce::String subtitle, juce::String badge,
           juce::Colour colour, bool defaultOn = true);
    virtual ~Pedal() = default;

    const juce::String& getId() const noexcept     { return id; }
    const juce::String& getName() const noexcept   { return name; }
    const juce::String& getSubtitle() const noexcept { return subtitle; }  // e.g. "Cassette preamp"
    const juce::String& getBadge() const noexcept  { return badge; }      // short mark printed on the face
    juce::Colour getColour() const noexcept        { return colour; }
    juce::String getOnParamId() const              { return id + "_on"; }
    const std::vector<KnobInfo>& getKnobs() const  { return knobs; }
    const std::vector<KnobInfo>& getSwitches() const { return switches; } // mini toggles (bool params)

    void addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    void attach (juce::AudioProcessorValueTreeState& state);

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

    virtual int getLatencySamples() const { return 0; }

protected:
    // Helpers for subclasses.
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    void addFloat (Layout& layout, const juce::String& suffix, const juce::String& knobLabel,
                   juce::NormalisableRange<float> range, float defaultValue,
                   std::function<juce::String (float)> toText = {});
    void addSwitch (Layout& layout, const juce::String& suffix, const juce::String& switchLabel, bool defaultValue);
    std::atomic<float>* param (juce::AudioProcessorValueTreeState& state, const juce::String& suffix) const;

    virtual void addPedalParameters (Layout& layout) = 0;
    virtual void attachPedal (juce::AudioProcessorValueTreeState& state) = 0;
    virtual void preparePedal (double sampleRate, int maxBlockSize) = 0;
    virtual void resetPedal() = 0;

    // Processes 'numSamples' of stereo audio in place.
    // For trails pedals, 'sendGain' (0..1 per sample) is the footswitch ramp: the
    // pedal must scale only the signal it feeds into its effect by it and always
    // pass the dry signal through. For other pedals 'sendGain' is nullptr and the
    // base class does the dry/wet crossfade.
    virtual void processPedal (juce::AudioBuffer<float>& buffer, int numSamples, const float* sendGain) = 0;

    virtual bool usesTrails() const { return false; }

    double sampleRate = 44100.0;

private:
    juce::String id, name, subtitle, badge;
    juce::Colour colour;
    bool defaultOn;
    std::vector<KnobInfo> knobs, switches;

    std::atomic<float>* onParam = nullptr;
    juce::SmoothedValue<float> onRamp;
    juce::AudioBuffer<float> dryBuffer;
    std::vector<float> rampBuffer;

    // Delays the dry signal by the pedal's latency so the bypass crossfade lines up.
    juce::AudioBuffer<float> dryDelay;
    int dryDelayPos = 0;
};

} // namespace orion
