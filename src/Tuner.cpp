#include "Tuner.h"
#include <cmath>

namespace orion
{

InputTap::InputTap() : ring ((size_t) capacity, 0.0f) {}

void InputTap::push (const float* left, const float* right, int numSamples) noexcept
{
    auto pos = written.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i, ++pos)
        ring[(size_t) (pos & (capacity - 1))] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];

    written.store (pos, std::memory_order_release);
}

bool InputTap::copyLatest (float* dest, int numSamples) const noexcept
{
    jassert (numSamples <= capacity / 2);   // so the writer can't lap the part being read
    const auto end = written.load (std::memory_order_acquire);
    if (end < (juce::uint64) numSamples)
        return false;

    auto pos = end - (juce::uint64) numSamples;
    for (int i = 0; i < numSamples; ++i, ++pos)
        dest[i] = ring[(size_t) (pos & (capacity - 1))];

    return true;
}

// ---------------------------------------------------------------------------
float PitchDetector::Biquad::process (float x) noexcept
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

void PitchDetector::prepare (double sampleRate)
{
    factor = juce::jmax (1, juce::roundToInt (sampleRate / 24000.0));
    analysisRate = sampleRate / factor;

    minLag = (int) std::floor (analysisRate / maxFrequency);
    maxLag = (int) std::ceil (analysisRate / minFrequency) + 2;
    window = juce::roundToInt (analysisRate * 0.064);   // 64 ms: over 3 periods of drop A
    warmup = 64;                                      // lets the low-pass settle

    frame.assign ((size_t) (window + maxLag), 0.0f);
    difference.assign ((size_t) maxLag + 1, 0.0f);
    normalised.assign ((size_t) maxLag + 1, 1.0f);
    requiredInput = (warmup + window + maxLag) * factor;

    // 4th-order Butterworth low-pass at 5 kHz: anti-aliasing for the decimation, and it
    // tames the upper harmonics, which carry no pitch information a tuner needs.
    const double w0 = juce::MathConstants<double>::twoPi * juce::jmin (5000.0, analysisRate * 0.4) / sampleRate;
    const double qs[] = { 0.54119610, 1.30656296 };

    for (int i = 0; i < 2; ++i)
    {
        const double alpha = std::sin (w0) / (2.0 * qs[i]), cosw = std::cos (w0), a0 = 1.0 + alpha;
        auto& f = lowpass[i];
        f.b0 = f.b2 = (float) ((1.0 - cosw) * 0.5 / a0);
        f.b1 = (float) ((1.0 - cosw) / a0);
        f.a1 = (float) (-2.0 * cosw / a0);
        f.a2 = (float) ((1.0 - alpha) / a0);
    }
}

PitchReading PitchDetector::analyse (const float* samples)
{
    jassert (requiredInput > 0);   // call prepare() first
    PitchReading reading;

    // Low-pass and decimate into 'frame', dropping the warm-up.
    for (auto& f : lowpass)
        f.z1 = f.z2 = 0.0f;

    const int frameSize = (int) frame.size();
    for (int i = 0, out = -warmup; out < frameSize; ++out)
        for (int k = 0; k < factor; ++k, ++i)
        {
            const float y = lowpass[1].process (lowpass[0].process (samples[i]));
            if (k == factor - 1 && out >= 0)
                frame[(size_t) out] = y;
        }

    // Remove DC and measure the level.
    double mean = 0.0;
    for (auto x : frame)
        mean += x;
    mean /= frameSize;

    double energy = 0.0;
    for (auto& x : frame)
    {
        x -= (float) mean;
        energy += (double) x * x;
    }
    reading.levelDb = juce::Decibels::gainToDecibels ((float) std::sqrt (energy / frameSize), -100.0f);
    if (reading.levelDb < gateDb)
        return reading;

    // YIN difference function and its cumulative mean normalised form.
    const float* x = frame.data();
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const float d = x[j] - x[j + lag];
            sum += (double) d * d;
        }
        difference[(size_t) lag] = (float) sum;
    }

    double running = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        running += difference[(size_t) lag];
        normalised[(size_t) lag] = running > 0.0 ? (float) ((double) difference[(size_t) lag] * lag / running) : 1.0f;
    }

    // The first dip below the threshold is the period; later dips are its multiples.
    constexpr float threshold = 0.15f, giveUp = 0.35f;
    int best = -1;
    for (int lag = juce::jmax (2, minLag); lag < maxLag; ++lag)
        if (normalised[(size_t) lag] < threshold)
        {
            while (lag + 1 < maxLag && normalised[(size_t) lag + 1] < normalised[(size_t) lag])
                ++lag;
            best = lag;
            break;
        }

    if (best < 0)
    {
        best = juce::jmax (2, minLag);
        for (int lag = best + 1; lag < maxLag; ++lag)
            if (normalised[(size_t) lag] < normalised[(size_t) best])
                best = lag;

        if (normalised[(size_t) best] > giveUp)
            return reading;
    }

    // Sub-sample period: parabolic interpolation on the raw difference.
    const auto refine = [this] (int lag)
    {
        const float a = difference[(size_t) lag - 1], b = difference[(size_t) lag], c = difference[(size_t) lag + 1];
        const float curvature = a - 2.0f * b + c;
        return (double) lag + (curvature > 0.0f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / curvature) : 0.0f);
    };

    double period = refine (best);

    // High notes span only a few samples per period, so measure across several periods
    // instead: the interpolation error is then divided by the number of periods. Only
    // used when there's a clear dip right where expected and it agrees with the
    // single-period estimate (real strings' overtones are slightly sharp).
    if (const int periods = juce::jmin (8, (int) ((maxLag - 2) / period)); periods >= 2)
    {
        const int expected = juce::roundToInt (period * periods);
        int lag = expected;
        for (int candidate : { expected - 1, expected + 1 })
            if (difference[(size_t) candidate] < difference[(size_t) lag])
                lag = candidate;

        const bool isDip = difference[(size_t) lag] <= difference[(size_t) lag - 1] && difference[(size_t) lag] <= difference[(size_t) lag + 1];
        const double refined = refine (lag) / periods;

        if (isDip && std::abs (refined - period) < period * 0.006)   // within ~10 cents
            period = refined;
    }

    const float frequency = (float) (analysisRate / period);
    if (frequency < minFrequency || frequency > maxFrequency)
        return reading;

    reading.frequency = frequency;
    reading.clarity = juce::jlimit (0.0f, 1.0f, 1.0f - normalised[(size_t) best]);
    return reading;
}

// ---------------------------------------------------------------------------
NoteReading toNote (float frequency)
{
    NoteReading note;
    if (! (frequency > 0.0f))
        return note;

    const double semitones = 69.0 + 12.0 * std::log2 (frequency / 440.0);
    note.midiNote = (int) std::lround (semitones);
    note.cents = (float) ((semitones - note.midiNote) * 100.0);
    return note;
}

juce::String NoteReading::getName() const
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return midiNote >= 0 ? names[midiNote % 12] : "";
}

int NoteReading::getOctave() const
{
    return midiNote / 12 - 1;
}

} // namespace orion
