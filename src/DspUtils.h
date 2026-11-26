#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Small, allocation-free (after prepare) DSP building blocks shared by the pedals.
namespace orion::dsp
{

constexpr float pi = juce::MathConstants<float>::pi;
constexpr float twoPi = juce::MathConstants<float>::twoPi;

// RBJ-cookbook biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowPass (double sr, double freq, double q = 0.7071) noexcept
    {
        const auto [w, cosw, alpha] = prewarp (sr, freq, q);
        juce::ignoreUnused (w);
        set ((1.0 - cosw) * 0.5, 1.0 - cosw, (1.0 - cosw) * 0.5, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    void setHighPass (double sr, double freq, double q = 0.7071) noexcept
    {
        const auto [w, cosw, alpha] = prewarp (sr, freq, q);
        juce::ignoreUnused (w);
        set ((1.0 + cosw) * 0.5, -(1.0 + cosw), (1.0 + cosw) * 0.5, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    void setPeak (double sr, double freq, double q, double gainDb) noexcept
    {
        const auto [w, cosw, alpha] = prewarp (sr, freq, q);
        juce::ignoreUnused (w);
        const double A = std::pow (10.0, gainDb / 40.0);
        set (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
    }

    // Shelving filters (RBJ, shelf slope S = 1).
    void setLowShelf (double sr, double freq, double gainDb) noexcept
    {
        const auto [w, c, alpha] = prewarp (sr, freq, 0.7071);
        juce::ignoreUnused (w);
        const double A = std::pow (10.0, gainDb / 40.0), sa = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) - (A - 1.0) * c + sa), 2.0 * A * ((A - 1.0) - (A + 1.0) * c), A * ((A + 1.0) - (A - 1.0) * c - sa),
             (A + 1.0) + (A - 1.0) * c + sa, -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - sa);
    }

    void setHighShelf (double sr, double freq, double gainDb) noexcept
    {
        const auto [w, c, alpha] = prewarp (sr, freq, 0.7071);
        juce::ignoreUnused (w);
        const double A = std::pow (10.0, gainDb / 40.0), sa = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1.0) + (A - 1.0) * c + sa), -2.0 * A * ((A - 1.0) + (A + 1.0) * c), A * ((A + 1.0) + (A - 1.0) * c - sa),
             (A + 1.0) - (A - 1.0) * c + sa, 2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - sa);
    }

private:
    struct Warp { double w, cosw, alpha; };

    static Warp prewarp (double sr, double freq, double q) noexcept
    {
        const double f = juce::jlimit (5.0, sr * 0.49, freq);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        return { w, std::cos (w), std::sin (w) / (2.0 * q) };
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = (float) (nb0 / na0);
        b1 = (float) (nb1 / na0);
        b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0);
        a2 = (float) (na2 / na0);
    }
};

// One-pole low-pass (|H| <= 1 at all frequencies, so it is safe inside feedback loops).
struct OnePoleLP
{
    float a = 0.0f, z = 0.0f;

    void reset() noexcept { z = 0.0f; }
    void setCutoff (double sr, double freq) noexcept { a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * juce::jlimit (5.0, sr * 0.49, freq) / sr); }
    float process (float x) noexcept { z = x + a * (z - x); return z; }
};

// One-pole high-pass built from the low-pass (|H| <= 1).
struct OnePoleHP
{
    OnePoleLP lp;

    void reset() noexcept { lp.reset(); }
    void setCutoff (double sr, double freq) noexcept { lp.setCutoff (sr, freq); }
    float process (float x) noexcept { return x - lp.process (x); }
};

// First-order all-pass: H(z) = (a + z^-1) / (1 + a z^-1). Unity gain, |a| < 1.
struct AllPass1
{
    float xPrev = 0.0f, yPrev = 0.0f;

    void reset() noexcept { xPrev = yPrev = 0.0f; }

    float process (float x, float a) noexcept
    {
        const float y = a * (x - yPrev) + xPrev;
        xPrev = x;
        yPrev = y;
        return y;
    }
};

// Circular delay line with fractional (cubic Hermite) reads.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    // Delay is measured from the most recently pushed sample (delay 0 = last written sample).
    float readInt (int delay) const noexcept
    {
        return buffer[(size_t) ((writePos - 1 - delay) & mask)];
    }

    float readCubic (float delay) const noexcept
    {
        delay = juce::jlimit (1.0f, (float) (mask - 4), delay);
        const int   d = (int) delay;
        const float t = delay - (float) d;

        const float xm1 = readInt (d - 1);
        const float x0  = readInt (d);
        const float x1  = readInt (d + 1);
        const float x2  = readInt (d + 2);

        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

// Sine LFO.
struct Lfo
{
    float phase = 0.0f, inc = 0.0f;

    void setRate (double sr, double hz) noexcept { inc = (float) (hz / sr); }
    void reset (float startPhase = 0.0f) noexcept { phase = startPhase; }

    float next() noexcept
    {
        const float v = std::sin (twoPi * phase);
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        return v;
    }
};

// Slowly wandering random value in [-1, 1] (smoothed noise) for tape drift.
struct Drift
{
    juce::Random rng { 0x0dde };
    OnePoleLP smooth;
    float held = 0.0f;
    int counter = 0, period = 1;

    void prepare (double sr, double rateHz)
    {
        period = juce::jmax (1, (int) (sr / rateHz));
        smooth.setCutoff (sr, rateHz);
        reset();
    }

    void reset() noexcept { smooth.reset(); held = 0.0f; counter = 0; }

    float next() noexcept
    {
        if (--counter <= 0)
        {
            counter = period;
            held = rng.nextFloat() * 2.0f - 1.0f;
        }
        return smooth.process (held);
    }
};

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

// Bounded soft clipper; |softClip(x, k)| < 1 / k for k >= 1.
inline float softClip (float x, float drive) noexcept { return std::tanh (x * drive) / drive; }

} // namespace orion::dsp
