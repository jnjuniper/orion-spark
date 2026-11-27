#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace orion
{

// The most recent input, written by the audio thread and read by the tuner display.
// One writer, one reader, no locks and no allocation after construction.
class InputTap
{
public:
    InputTap();

    void setSampleRate (double rate) noexcept      { sampleRate = rate; }
    double getSampleRate() const noexcept          { return sampleRate.load(); }

    // Audio thread. Stores the average of 'left' and 'right' ('right' may be nullptr).
    void push (const float* left, const float* right, int numSamples) noexcept;

    // Reader. Copies the newest 'numSamples' (at most capacity / 2) into 'dest'.
    // Returns false if fewer than that have been written so far.
    bool copyLatest (float* dest, int numSamples) const noexcept;

    static constexpr int capacity = 1 << 16;   // > 0.3 s at 192 kHz

private:
    std::vector<float> ring;
    std::atomic<juce::uint64> written { 0 };
    std::atomic<double> sampleRate { 44100.0 };
};

struct PitchReading
{
    float frequency = 0.0f;   // Hz; 0 when there's no clear pitch
    float clarity = 0.0f;     // 0..1, how periodic the signal is
    float levelDb = -100.0f;

    bool hasPitch() const noexcept { return frequency > 0.0f; }
};

// YIN pitch detection over the guitar range, from drop A (55 Hz) up to the 24th
// fret of the high E string. The input is low-passed and decimated to about 24 kHz
// first, so the window length and CPU cost are the same at any host sample rate.
class PitchDetector
{
public:
    void prepare (double sampleRate);

    // How many input samples (at the host rate) each call to analyse() needs.
    int getRequiredInputSamples() const noexcept { return requiredInput; }

    // 'samples' must hold getRequiredInputSamples() samples, oldest first.
    PitchReading analyse (const float* samples);

    static constexpr float minFrequency = 50.0f;     // a little below drop A
    static constexpr float maxFrequency = 1400.0f;   // above the high E's 24th fret (1319 Hz)
    static constexpr float gateDb = -65.0f;          // quieter than this: no reading

private:
    double analysisRate = 0.0;
    int factor = 1, warmup = 0, window = 0, minLag = 0, maxLag = 0, requiredInput = 0;

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x) noexcept;
    };
    Biquad lowpass[2];

    std::vector<float> frame, difference, normalised;
};

// A frequency as the nearest note and how far off it is, with A4 = 440 Hz.
struct NoteReading
{
    int midiNote = -1;
    float cents = 0.0f;   // -50..+50: negative is flat

    juce::String getName() const;   // "E", "F#", ...
    int getOctave() const;          // scientific pitch: low E string is E2
};

NoteReading toNote (float frequency);

} // namespace orion
