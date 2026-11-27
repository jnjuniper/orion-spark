// Offline test runner for Orion Spark: pushes test signals through the real processor
// and checks the things that can be verified without ears.
//
//   ./OrionTests            -> exit code 0 if everything passed

#include "PluginProcessor.h"
#include "Presets.h"
#include "PluginEditor.h"
#include "Tuner.h"
#include "ui/Logo.h"
#include <cmath>
#include <functional>
#include <iostream>

namespace
{

int failures = 0;

void check (bool condition, const juce::String& what)
{
    std::cout << (condition ? "  [pass] " : "  [FAIL] ") << what << "\n";
    if (! condition)
        ++failures;
}

void setParam (OrionAudioProcessor& p, const juce::String& id, float value)
{
    auto* param = p.getState().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void setAllPedals (OrionAudioProcessor& p, bool on)
{
    for (auto& pedal : p.getPedals())
        setParam (p, pedal->getOnParamId(), on ? 1.0f : 0.0f);
}

struct RunResult
{
    std::vector<float> left, right;
    bool finite = true;
    float peak = 0.0f;

    float rms (double sr, double fromSec, double toSec) const
    {
        const auto a = (size_t) (fromSec * sr), b = juce::jmin (left.size(), (size_t) (toSec * sr));
        double sum = 0.0;
        for (auto i = a; i < b; ++i)
            sum += 0.5 * ((double) left[i] * left[i] + (double) right[i] * right[i]);
        return b > a ? (float) std::sqrt (sum / (double) (b - a)) : 0.0f;
    }
};

using Signal = std::function<float (int sampleIndex)>;
using BlockHook = std::function<void (OrionAudioProcessor&, juce::Random&)>;

// Mono signal in (as a host would deliver it), stereo out.
RunResult run (OrionAudioProcessor& p, double sr, int blockSize, double seconds, const Signal& signal,
               const BlockHook& perBlock = {}, bool garbageInSecondChannel = false, const Signal& rightSignal = {})
{
    if (rightSignal)
    {
        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add (juce::AudioChannelSet::stereo());
        stereo.outputBuses.add (juce::AudioChannelSet::stereo());
        const bool ok = p.setBusesLayout (stereo);
        jassert (ok);
        juce::ignoreUnused (ok);
    }

    p.setPlayConfigDetails (rightSignal ? 2 : 1, 2, sr, blockSize);
    p.prepareToPlay (sr, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng (1234);

    RunResult r;
    const int total = (int) (seconds * sr);
    r.left.reserve ((size_t) total);
    r.right.reserve ((size_t) total);

    for (int start = 0; start < total; start += blockSize)
    {
        const int n = juce::jmin (blockSize, total - start);
        buffer.setSize (2, n, false, false, true);

        for (int i = 0; i < n; ++i)
        {
            buffer.setSample (0, i, signal (start + i));
            buffer.setSample (1, i, rightSignal ? rightSignal (start + i)
                                                : (garbageInSecondChannel ? rng.nextFloat() * 2.0f - 1.0f : 0.0f));
        }

        if (perBlock)
            perBlock (p, rng);

        p.processBlock (buffer, midi);

        for (int i = 0; i < n; ++i)
        {
            const float l = buffer.getSample (0, i), rr = buffer.getSample (1, i);
            if (! std::isfinite (l) || ! std::isfinite (rr))
                r.finite = false;
            r.peak = juce::jmax (r.peak, std::abs (l), std::abs (rr));
            r.left.push_back (l);
            r.right.push_back (rr);
        }
    }

    return r;
}

float toDb (float x) { return juce::Decibels::gainToDecibels (x, -200.0f); }

Signal impulse (float amp) { return [amp] (int i) { return i == 0 ? amp : 0.0f; }; }

Signal noiseBurst (double sr, double seconds, float amp)
{
    auto rng = std::make_shared<juce::Random> (42);
    const int len = (int) (seconds * sr);
    return [=] (int i) { return i < len ? amp * (rng->nextFloat() * 2.0f - 1.0f) : 0.0f; };
}

Signal sweep (double sr, double seconds, float amp)
{
    return [=] (int i)
    {
        const double t = i / sr;
        if (t >= seconds)
            return 0.0f;
        const double f0 = 20.0, f1 = 20000.0, k = std::log (f1 / f0) / seconds;
        return amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f0 * (std::exp (k * t) - 1.0) / k);
    };
}

// -----------------------------------------------------------------------------
void testDefaultsAcrossRatesAndBlocks()
{
    std::cout << "\nDefaults: impulse at several sample rates / block sizes\n";

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int bs : { 1, 37, 64, 512, 2048 })
        {
            OrionAudioProcessor p;
            const auto r = run (p, sr, bs, 12.0, impulse (0.5f));
            const float tail = r.rms (sr, 11.5, 12.0);
            check (r.finite && r.peak < 2.0f && toDb (tail) < -90.0f,
                   juce::String (sr, 0) + " Hz / block " + juce::String (bs)
                       + ": finite, peak " + juce::String (toDb (r.peak), 1) + " dBFS, tail "
                       + juce::String (toDb (tail), 1) + " dBFS");
        }
}

void testSweep()
{
    std::cout << "\nDefaults: -6 dBFS sine sweep\n";
    OrionAudioProcessor p;
    const double sr = 48000.0;
    const auto r = run (p, sr, 256, 14.0, sweep (sr, 4.0, 0.5f));
    check (r.finite, "finite output");
    check (r.peak < 4.0f, "peak bounded (" + juce::String (toDb (r.peak), 1) + " dBFS)");
    check (toDb (r.rms (sr, 13.5, 14.0)) < -90.0f, "decays to silence");
}

void testSilenceInSilenceOut()
{
    std::cout << "\nSilence in -> silence out (defaults, hiss off)\n";
    OrionAudioProcessor p;
    const auto r = run (p, 48000.0, 512, 3.0, [] (int) { return 0.0f; });
    check (r.finite && r.peak < 1.0e-7f, "output is silent (peak " + juce::String (r.peak) + ")");
}

void testWorstCaseSettings()
{
    std::cout << "\nWorst case: every knob at max, full-scale noise burst\n";
    const double sr = 48000.0;

    for (float echoTime : { 40.0f, 1200.0f })
        for (float age : { 0.0f, 1.0f })
          for (bool flanger : { false, true })
        {
            OrionAudioProcessor p;
            setAllPedals (p, true);
            setParam (p, "od_drive", 1.0f);
            setParam (p, "od_tone", 1.0f);
            setParam (p, "od_level", 18.0f);
            for (auto id : { "amp_gain", "amp_bass", "amp_mid", "amp_treble", "amp_presence" })
                setParam (p, id, 1.0f);
            setParam (p, "amp_master", 12.0f);
            setParam (p, "mod_rate", 8.0f);
            setParam (p, "mod_depth", 1.0f);
            setParam (p, "mod_regen", 1.0f);
            setParam (p, "mod_mix", 1.0f);
            setParam (p, "mod_flanger", flanger ? 1.0f : 0.0f);
            setParam (p, "in_gain", 24.0f);
            setParam (p, "out_gain", 12.0f);
            setParam (p, "pre_drive", 1.0f);
            setParam (p, "pre_level", 12.0f);
            setParam (p, "pre_wobble", 1.0f);
            setParam (p, "pre_hiss", 0.0f);
            setParam (p, "echo_time", echoTime);
            setParam (p, "echo_repeats", 1.0f);
            setParam (p, "echo_mix", 1.0f);
            setParam (p, "echo_age", age);
            setParam (p, "echo_wobble", 1.0f);
            setParam (p, "spring_decay", 1.0f);
            setParam (p, "spring_mix", 1.0f);
            setParam (p, "spring_drip", 1.0f);
            setParam (p, "gate_threshold", -90.0f);

            const auto r = run (p, sr, 512, 40.0, noiseBurst (sr, 2.0, 1.0f));
            const float early = r.rms (sr, 3.0, 5.0);
            const float late = r.rms (sr, 38.0, 40.0);
            const auto name = "time " + juce::String (echoTime, 0) + " ms, age " + juce::String (age, 0)
                              + (flanger ? ", flanger" : ", chorus");
            check (r.finite, name + ": finite");
            check (r.peak < 64.0f, name + ": bounded (peak " + juce::String (toDb (r.peak), 1) + " dBFS)");
            check (late < early * 0.9f, name + ": feedback decays (" + juce::String (toDb (early), 1)
                                            + " -> " + juce::String (toDb (late), 1) + " dB RMS)");
        }
}

void testBypassIsTransparent()
{
    std::cout << "\nBypass: all pedals off == input delayed by reported latency\n";

    for (double sr : { 44100.0, 96000.0 })
    {
        OrionAudioProcessor p;
        setAllPedals (p, false);
        auto in = noiseBurst (sr, 10.0, 0.5f);

        std::vector<float> input;
        const auto r = run (p, sr, 333, 1.0, [&] (int i) { const float x = in (i); input.push_back (x); return x; },
                            {}, true);

        const int latency = p.getLatencySamples();
        float maxErr = 0.0f;
        for (size_t i = (size_t) latency; i < r.left.size(); ++i)
        {
            const float expected = input[i - (size_t) latency];
            maxErr = juce::jmax (maxErr, std::abs (r.left[i] - expected), std::abs (r.right[i] - expected));
        }

        check (maxErr < 1.0e-6f, juce::String (sr, 0) + " Hz: latency " + juce::String (latency)
                                     + " samples, max error " + juce::String (maxErr));
    }
}

void testAutomationStress()
{
    std::cout << "\nAutomation stress: random parameter changes every block\n";
    const double sr = 48000.0;
    OrionAudioProcessor p;

    juce::StringArray ids;
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            ids.add (withId->paramID);

    const auto r = run (p, sr, 128, 20.0, noiseBurst (sr, 20.0, 0.25f),
                        [&] (OrionAudioProcessor& proc, juce::Random& rng)
                        {
                            if (rng.nextInt (4) == 0)
                                proc.getState().getParameter (ids[rng.nextInt (ids.size())])->setValueNotifyingHost (rng.nextFloat());
                        });

    check (r.finite, "finite output");
    check (r.peak < 64.0f, "bounded (peak " + juce::String (toDb (r.peak), 1) + " dBFS)");
}

void testStateRoundTrip()
{
    std::cout << "\nState save / restore\n";
    OrionAudioProcessor a;
    setParam (a, "echo_time", 777.0f);
    setParam (a, "pre_on", 0.0f);

    juce::MemoryBlock data;
    a.getStateInformation (data);

    OrionAudioProcessor b;
    b.setStateInformation (data.getData(), (int) data.getSize());
    const float time = b.getState().getRawParameterValue ("echo_time")->load();
    const float preOn = b.getState().getRawParameterValue ("pre_on")->load();
    check (std::abs (time - 777.0f) < 0.5f && preOn < 0.5f, "parameters restored");
}


void testStereoInput()
{
    std::cout << "\nStereo input (as Ableton may negotiate)\n";
    const double sr = 48000.0;

    {
        OrionAudioProcessor p;
        setAllPedals (p, false);
        auto l = noiseBurst (sr, 10.0, 0.5f);
        auto r = sweep (sr, 10.0, 0.3f);
        std::vector<float> inL, inR;
        const auto res = run (p, sr, 256, 1.0,
                              [&] (int i) { const float x = l (i); inL.push_back (x); return x; },
                              {}, false,
                              [&] (int i) { const float x = r (i); inR.push_back (x); return x; });
        const int latency = p.getLatencySamples();
        float maxErr = 0.0f;
        for (size_t i = (size_t) latency; i < res.left.size(); ++i)
            maxErr = juce::jmax (maxErr, std::abs (res.left[i] - inL[i - (size_t) latency]),
                                 std::abs (res.right[i] - inR[i - (size_t) latency]));
        check (p.getTotalNumInputChannels() == 2, "stereo layout accepted");
        check (maxErr < 1.0e-6f, "bypass keeps L and R separate and aligned (max error " + juce::String (maxErr) + ")");
    }
    {
        OrionAudioProcessor p;
        const auto res = run (p, sr, 256, 12.0, sweep (sr, 4.0, 0.5f), {}, false, noiseBurst (sr, 4.0, 0.5f));
        check (res.finite && res.peak < 4.0f, "defaults with stereo input: finite, peak " + juce::String (toDb (res.peak), 1) + " dBFS");
        check (toDb (res.rms (sr, 11.5, 12.0)) < -90.0f, "defaults with stereo input: decays to silence");
    }
}

void testWetPathLatency()
{
    std::cout << "\nReported latency matches the processed (wet) path\n";

    struct Config { const char* name; std::function<void (OrionAudioProcessor&)> setup; float amplitude; };
    const Config configs[] =
    {
        { "4-Track Pre", [] (OrionAudioProcessor& p)
          {
              setParam (p, "pre_on", 1.0f);
              setParam (p, "pre_drive", 0.0f);
              setParam (p, "pre_wobble", 0.0f);
              setParam (p, "pre_tone", 1.0f);
          }, 0.25f },
        // Amp near-linear: low gain, quiet tone, flat tone stack, no presence, cab off.
        { "Brit Stack", [] (OrionAudioProcessor& p)
          {
              setParam (p, "amp_on", 1.0f);
              setParam (p, "amp_gain", 0.0f);
              setParam (p, "amp_bass", 0.6f);
              setParam (p, "amp_mid", 0.6f);
              setParam (p, "amp_treble", 0.6f);
              setParam (p, "amp_presence", 0.0f);
              setParam (p, "amp_cab", 0.0f);
          }, 0.01f },
    };

    for (const auto& config : configs)
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            OrionAudioProcessor p;
            setAllPedals (p, false);
            config.setup (p);

            // A 1 kHz tone burst: compare the phase of input and output via cross-correlation.
            const int start = (int) (0.2 * sr), len = (int) (0.05 * sr);
            const float amp = config.amplitude;
            auto tone = [=] (int i)
            {
                if (i < start || i >= start + len) return 0.0f;
                const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (i - start) / len);
                return (float) (amp * w * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
            };
            const auto res = run (p, sr, 512, 0.5, tone);

            int bestLag = 0;
            double best = -1.0e9;
            for (int lag = 0; lag < (int) (0.02 * sr); ++lag)
            {
                double c = 0.0;
                for (int i = start; i < start + len && (size_t) (i + lag) < res.left.size(); ++i)
                    c += (double) tone (i) * res.left[(size_t) (i + lag)];
                if (c > best) { best = c; bestLag = lag; }
            }

            const int latency = p.getLatencySamples();
            check (std::abs (bestLag - latency) <= 2, juce::String (config.name) + ", " + juce::String (sr, 0) + " Hz: measured "
                                                         + juce::String (bestLag) + " vs reported " + juce::String (latency) + " samples");
        }
}

void reportLevels()
{
    std::cout << "\nLoudness at default settings (-18 dBFS 196 Hz sine, typical guitar DI level)\n";
    const double sr = 48000.0;
    auto sine = [=] (int i) { return 0.1259f * (float) std::sqrt (2.0) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 196.0 * i / sr); };

    for (const auto& only : juce::StringArray { "pre", "od", "amp", "all" })
    {
        OrionAudioProcessor p;
        if (only != "all")
        {
            setAllPedals (p, false);
            setParam (p, only + "_on", 1.0f);
        }
        const auto res = run (p, sr, 512, 2.0, sine);
        const float outDb = toDb (res.rms (sr, 1.0, 2.0));
        const auto label = only == "all" ? juce::String ("whole board (defaults)")
                         : only == "pre" ? juce::String ("4-Track Pre only")
                         : only == "od"  ? juce::String ("Overdrive only")
                                         : juce::String ("Brit Stack only");
        check (std::abs (outDb + 18.0f) < 4.0f, label
                                                    + ": " + juce::String (outDb, 1) + " dBFS RMS out vs -18.0 in");
    }

    // The amp's auto-gain should keep loudness roughly steady across the Gain knob
    // (wider tolerance: heavy clipping changes the waveform's crest factor).
    for (float gain : { 0.0f, 1.0f })
    {
        OrionAudioProcessor p;
        setAllPedals (p, false);
        setParam (p, "amp_on", 1.0f);
        setParam (p, "amp_gain", gain);
        const auto res = run (p, sr, 512, 2.0, sine);
        const float outDb = toDb (res.rms (sr, 1.0, 2.0));
        check (std::abs (outDb + 18.0f) < 6.0f, "Brit Stack, gain " + juce::String (gain, 0) + ": "
                                                    + juce::String (outDb, 1) + " dBFS RMS out vs -18.0 in");
    }
}

void testPresets()
{
    std::cout << "\nFactory presets: load, play a -18 dBFS riff-like signal, check stability and loudness\n";
    const double sr = 48000.0;
    const auto& presets = orion::getFactoryPresets();

    // Decaying plucked notes, roughly guitar-like, peaking around -12 dBFS.
    auto riff = [=] (int i)
    {
        const double t = i / sr;
        const double noteLen = 0.25;
        const int note = (int) (t / noteLen);
        const double tn = t - note * noteLen;
        const double f = 82.4 * std::pow (2.0, ((note * 5) % 12) / 12.0);
        return (float) (0.25 * std::exp (-tn * 6.0) * (std::sin (2.0 * juce::MathConstants<double>::pi * f * t)
                                                        + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 2.0 * f * t)));
    };

    float inRms = 0.0f;
    {
        double sum = 0.0;
        const int n = (int) (2.0 * sr);
        for (int i = 0; i < n; ++i) sum += (double) riff (i) * riff (i);
        inRms = (float) std::sqrt (sum / n);
    }

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        OrionAudioProcessor p;
        p.loadFactoryPreset (i);
        const bool applied = ! p.getCurrentPreset().isUser && p.getCurrentPreset().name == presets[(size_t) i].name;
        const auto r = run (p, sr, 256, 6.0, [&] (int n) { return n < (int) (4.0 * sr) ? riff (n) : 0.0f; });
        const float outDb = toDb (r.rms (sr, 0.5, 4.0)) - toDb (inRms);
        check (applied && r.finite && r.peak < 4.0f && std::abs (outDb) < 9.0f,
               presets[(size_t) i].name.paddedRight (' ', 20) + ": peak " + juce::String (toDb (r.peak), 1)
                   + " dBFS, loudness " + (outDb >= 0 ? "+" : "") + juce::String (outDb, 1) + " dB vs input");
    }

    // Spot-check that a preset really sets the board.
    OrionAudioProcessor p;
    p.loadFactoryPreset (2);
    auto& st = p.getState();
    check (st.getRawParameterValue ("amp_on")->load() > 0.5f && std::abs (st.getRawParameterValue ("echo_time")->load() - 420.0f) < 1.0f
               && st.getRawParameterValue ("pre_on")->load() < 0.5f,
           "\"" + presets[2].name + "\" sets amp on, echo 420 ms, 4-Track off");
}

void testPresetStateRoundTrip()
{
    std::cout << "\nPreset + tweaks survive save / restore (as in an Ableton set)\n";
    const auto& presets = orion::getFactoryPresets();
    OrionAudioProcessor a;
    a.loadFactoryPreset (2);
    setParam (a, "amp_gain", 0.33f);

    juce::MemoryBlock data;
    a.getStateInformation (data);

    OrionAudioProcessor b;
    b.setStateInformation (data.getData(), (int) data.getSize());
    const float gain = b.getState().getRawParameterValue ("amp_gain")->load();
    check (b.getCurrentPreset().name == presets[2].name && ! b.getCurrentPreset().isUser && std::abs (gain - 0.33f) < 0.01f,
           "preset \"" + b.getCurrentPreset().name + "\", tweaked amp gain " + juce::String (gain, 2));
    check (b.isPresetModified(), "restored tweaks still show as modified");

    // Sessions saved before user presets stored the factory preset as a list index.
    OrionAudioProcessor legacy;
    {
        auto tree = a.getState().copyState();
        tree.removeProperty ("presetName", nullptr);
        tree.removeProperty ("presetIsUser", nullptr);
        tree.setProperty ("preset", 3, nullptr);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
        legacy.setStateInformation (old.getData(), (int) old.getSize());
    }
    check (legacy.getCurrentPreset().name == presets[3].name && ! legacy.getCurrentPreset().isUser,
           "old session with preset index 3 shows \"" + legacy.getCurrentPreset().name + "\"");
}

// A fresh, empty preset folder that is removed again afterwards.
struct TempPresetFolder
{
    TempPresetFolder()  { folder.deleteRecursively(); folder.createDirectory(); }
    ~TempPresetFolder() { folder.deleteRecursively(); }

    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("OrionTests-presets-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));

    orion::UserPresetLibrary library() const { return orion::UserPresetLibrary (folder, false); }   // no Trash in tests
};

void testUserPresets()
{
    std::cout << "\nUser presets: save, load, overwrite, rename, delete\n";
    TempPresetFolder temp;

    OrionAudioProcessor a (temp.library());
    a.loadFactoryPreset (2);
    check (! a.isPresetModified(), "freshly loaded factory preset is not modified");
    setParam (a, "amp_gain", 0.31f);
    setParam (a, "echo_time", 555.0f);
    setParam (a, "in_gain", 6.0f);
    check (a.isPresetModified(), "tweaking a knob marks it modified");

    check (a.saveUserPreset ("  My Crunch  ") && a.getUserPresets().exists ("My Crunch"), "Save As writes \"My Crunch\"");
    check (a.getCurrentPreset().isUser && a.getCurrentPreset().name == "My Crunch" && ! a.isPresetModified(),
           "saved preset becomes current and unmodified");

    // Load it into a fresh instance: same values, global input gain untouched.
    OrionAudioProcessor b (temp.library());
    const bool loaded = b.loadUserPreset ("My Crunch");
    auto& st = b.getState();
    check (loaded && std::abs (st.getRawParameterValue ("amp_gain")->load() - 0.31f) < 0.001f
               && std::abs (st.getRawParameterValue ("echo_time")->load() - 555.0f) < 0.5f
               && st.getRawParameterValue ("amp_on")->load() > 0.5f,
           "loads with the same values");
    check (std::abs (st.getRawParameterValue ("in_gain")->load()) < 0.01f, "input gain is not part of a preset");
    check (! b.isPresetModified(), "loaded user preset is not modified");

    // Overwrite.
    setParam (b, "amp_gain", 0.5f);
    check (b.isPresetModified(), "tweak marks it modified");
    check (b.saveUserPreset ("My Crunch") && ! b.isPresetModified(), "Save overwrites the user preset");
    a.loadUserPreset ("My Crunch");
    check (std::abs (a.getState().getRawParameterValue ("amp_gain")->load() - 0.5f) < 0.001f, "other instance sees the new values");

    // Session restore points at the user preset.
    juce::MemoryBlock data;
    b.getStateInformation (data);
    OrionAudioProcessor c (temp.library());
    c.setStateInformation (data.getData(), (int) data.getSize());
    check (c.getCurrentPreset().isUser && c.getCurrentPreset().name == "My Crunch" && ! c.isPresetModified(),
           "session restore shows \"My Crunch\", not modified");

    // Rename.
    check (b.renameUserPreset ("My Crunch", "Big Crunch") && b.getCurrentPreset().name == "Big Crunch"
               && ! b.getUserPresets().exists ("My Crunch") && b.getUserPresets().exists ("Big Crunch"),
           "rename moves the file and updates the current preset");
    check (b.saveUserPreset ("Other") && ! b.renameUserPreset ("Other", "Big Crunch"), "rename refuses to replace another preset");

    // Library listing.
    const auto list = b.getUserPresets().scan();
    check (list.size() == 2 && list[0].name == "Big Crunch" && list[1].name == "Other", "folder lists 2 presets, sorted");

    // Junk in the folder is ignored.
    temp.folder.getChildFile ("Broken.orionpreset").replaceWithText ("this is not xml");
    temp.folder.getChildFile ("Weird.orionpreset").replaceWithText (
        "<OrionPreset version=\"1\"><Param id=\"no_such_param\" value=\"1\"/><Param id=\"amp_gain\" value=\"nan\"/>"
        "<Param id=\"echo_time\" value=\"999999\"/></OrionPreset>");
    check (b.getUserPresets().scan().size() == 3, "unreadable file is skipped");
    OrionAudioProcessor d (temp.library());
    const bool weirdLoaded = d.loadUserPreset ("Weird");
    const float time = d.getState().getRawParameterValue ("echo_time")->load();
    const float maxTime = d.getState().getParameterRange ("echo_time").end;
    const float ampGain = d.getState().getRawParameterValue ("amp_gain")->load();
    check (weirdLoaded && std::isfinite (time) && time <= maxTime && std::isfinite (ampGain),
           "unknown IDs, NaN and out-of-range values are handled (echo " + juce::String (time, 0) + " ms)");

    // A file added in Finder whose name can't be a preset name isn't listed.
    temp.folder.getChildFile ("My, Crunch.orionpreset").replaceWithText ("<OrionPreset version=\"1\"/>");
    check (b.getUserPresets().scan().size() == 3, "file named \"My, Crunch\" is not listed");

    // Names.
    const juce::String nordic (juce::CharPointer_UTF8 ("\xc3\x85skv\xc3\xa4" "der \xc3\xb6st"));   // Åskväder öst
    const bool nordicSaved = b.saveUserPreset (nordic);
    bool nordicListed = false;
    for (const auto& preset : b.getUserPresets().scan())
        nordicListed = nordicListed || preset.name == nordic;
    check (nordicSaved && nordicListed && b.loadUserPreset (nordic) && b.getCurrentPreset().name == nordic, "non-ASCII name saves, lists and loads");

    using Lib = orion::UserPresetLibrary;
    check (Lib::cleanName ("a/b:c") == "abc" && Lib::cleanName ("..") .isEmpty() && Lib::cleanName ("   ").isEmpty()
               && Lib::cleanName ("Sykes '87") == "Sykes '87" && Lib::cleanName (juce::String::repeatedString ("x", 100)).length() == Lib::maxNameLength,
           "names are cleaned for use as file names");
    check (! b.saveUserPreset ("///"), "unusable name is refused");

    // Delete: the board keeps its sound and name but shows as unsaved.
    b.loadUserPreset ("Big Crunch");
    const bool deleted = b.deleteUserPreset ("Big Crunch");
    check (deleted && ! b.getUserPresets().exists ("Big Crunch") && b.getCurrentPreset().name == "Big Crunch" && b.isPresetModified(),
           "delete removes the file; the board keeps the name and shows modified");
    check (! c.loadUserPreset ("Big Crunch"), "loading a deleted preset fails cleanly");

    // A session whose user preset has since been deleted keeps its name and sound.
    b.saveUserPreset ("Gone Soon");
    juce::MemoryBlock gone;
    b.getStateInformation (gone);
    b.deleteUserPreset ("Gone Soon");
    OrionAudioProcessor e (temp.library());
    e.setStateInformation (gone.getData(), (int) gone.getSize());
    check (e.getCurrentPreset().isUser && e.getCurrentPreset().name == "Gone Soon"
               && std::abs (e.getState().getRawParameterValue ("amp_gain")->load() - b.getState().getRawParameterValue ("amp_gain")->load()) < 0.001f
               && e.isPresetModified(),
           "session with a deleted user preset restores its name and values, shown as modified");
}

void testCpu()
{
    std::cout << "\nCPU: every pedal on, 48 kHz, 64-sample blocks\n";
    const double sr = 48000.0;
    const int block = 64;
    OrionAudioProcessor p;
    setAllPedals (p, true);
    p.setPlayConfigDetails (1, 2, sr, block);
    p.prepareToPlay (sr, block);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    juce::Random rng (7);
    const double seconds = 20.0;
    const int blocks = (int) (seconds * sr / block);

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i)
            buffer.setSample (0, i, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));
        p.processBlock (buffer, midi);
    }
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
    const double load = elapsed / seconds * 100.0;
    check (load < 30.0, "uses " + juce::String (load, 1) + " % of one core in real time (" + juce::String (elapsed * 1.0e6 / blocks, 1)
                            + " us per 1.33 ms block)");
}

} // namespace

// Renders the editor to PNGs (default state, and with the echo bypassed) so the
// layout can be checked without a host. Also reports which fonts resolved.
namespace
{

// -----------------------------------------------------------------------------
// Guitar-like tone: weak fundamental, strong 2nd and 3rd harmonics, a little noise.
Signal guitarTone (double sr, double frequency, float amp)
{
    auto rng = std::make_shared<juce::Random> (7);
    return [=] (int i)
    {
        static constexpr float harmonics[] = { 0.15f, 1.0f, 0.6f, 0.5f, 0.3f, 0.25f, 0.15f, 0.1f };
        const double t = i / sr;
        double s = 0.0;
        for (int k = 0; k < 8; ++k)
            s += harmonics[k] * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * (k + 1) * t + 0.7 * k);
        return amp * 0.3f * (float) s + 0.0005f * (rng->nextFloat() * 2.0f - 1.0f);
    };
}

orion::PitchReading detect (orion::PitchDetector& detector, const Signal& signal, int offset = 12345)
{
    std::vector<float> samples ((size_t) detector.getRequiredInputSamples());
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = signal (offset + (int) i);
    return detector.analyse (samples.data());
}

float centsBetween (float detected, double expected) { return (float) (1200.0 * std::log2 (detected / expected)); }

void testTunerNotes()
{
    std::cout << "\nTuner: note names (A4 = 440 Hz)\n";
    const auto a4 = orion::toNote (440.0f), lowE = orion::toNote (82.407f), dropA = orion::toNote (55.0f);
    const auto sharpA = orion::toNote (440.0f * std::pow (2.0f, 17.0f / 1200.0f));
    const auto flatB = orion::toNote (246.94f * std::pow (2.0f, -30.0f / 1200.0f));
    check (a4.getName() == "A" && a4.getOctave() == 4 && std::abs (a4.cents) < 0.01f, "440 Hz is A4, 0 cents");
    check (lowE.getName() == "E" && lowE.getOctave() == 2 && std::abs (lowE.cents) < 0.1f, "82.41 Hz is E2 (low E string)");
    check (dropA.getName() == "A" && dropA.getOctave() == 1, "55 Hz is A1 (drop A)");
    check (sharpA.getName() == "A" && std::abs (sharpA.cents - 17.0f) < 0.01f, "A + 17 cents reads +17");
    check (flatB.getName() == "B" && std::abs (flatB.cents + 30.0f) < 0.05f, "B - 30 cents reads -30");
}

void testTunerAccuracy()
{
    std::cout << "\nTuner: pitch detection on guitar-like tones with a weak fundamental\n";
    const double notes[] = { 55.0, 61.735, 73.416, 82.407, 110.0, 146.83, 196.0, 246.94, 329.63,   // drop A .. high E
                             440.0, 659.26, 987.77, 1318.5 };                                     // up the neck

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        orion::PitchDetector detector;
        detector.prepare (sr);
        float worstLow = 0.0f, worstHigh = 0.0f;
        int missed = 0, wrongNote = 0;

        for (double note : notes)
            for (double detune : { -23.0, 0.0, 9.0 })
            {
                const double f = note * std::pow (2.0, detune / 1200.0);
                const auto reading = detect (detector, guitarTone (sr, f, 0.5f));
                if (! reading.hasPitch())
                {
                    ++missed;
                    continue;
                }

                const float err = std::abs (centsBetween (reading.frequency, f));
                if (orion::toNote (reading.frequency).midiNote != orion::toNote ((float) note).midiNote)
                    ++wrongNote;
                (note < 700.0 ? worstLow : worstHigh) = juce::jmax (note < 700.0 ? worstLow : worstHigh, err);
            }

        check (missed == 0 && wrongNote == 0, juce::String (sr, 0) + " Hz: every note found, no octave errors ("
                                                   + juce::String (missed) + " missed, " + juce::String (wrongNote) + " wrong)");
        check (worstLow < 1.0f && worstHigh < 1.0f, juce::String (sr, 0) + " Hz: within 1 cent (worst " + juce::String (worstLow, 2)
                                                         + " up to 700 Hz, " + juce::String (worstHigh, 2) + " above)");
    }

    orion::PitchDetector detector;
    detector.prepare (48000.0);

    // A plucked string fading quickly during the analysis window.
    float worstPluck = 0.0f;
    for (double f : { 82.407, 110.0, 329.63 })
    {
        const auto tone = guitarTone (48000.0, f, 0.5f);
        const auto reading = detect (detector, [=] (int i) { return tone (i) * (float) std::exp (-4.0 * i / 48000.0); }, 2400);
        worstPluck = juce::jmax (worstPluck, reading.hasPitch() ? std::abs (centsBetween (reading.frequency, f)) : 100.0f);
    }
    check (worstPluck < 1.0f, "decaying pluck: within 1 cent (worst " + juce::String (worstPluck, 2) + ")");

    // Real strings are stiff: overtones run slightly sharp (k * f * sqrt (1 + B k^2)), which
    // pulls any period-based reading (and the ear) a little above the fundamental. The
    // reading must stay on the right note and move smoothly with stiffness, without jumps.
    for (double b : { 1.0e-4, 5.0e-4 })
    {
        int wrong = 0;
        float lowest = 1000.0f, highest = -1000.0f;
        for (double f : { 82.407, 110.0, 196.0, 329.63, 659.26, 1318.5 })
        {
            const auto reading = detect (detector, [=] (int i)
            {
                static constexpr float harmonics[] = { 0.6f, 1.0f, 0.6f, 0.5f, 0.3f, 0.25f };
                const double t = i / 48000.0;
                double s = 0.0;
                for (int k = 1; k <= 6; ++k)
                    s += harmonics[k - 1] * std::sin (2.0 * juce::MathConstants<double>::pi * k * f * std::sqrt (1.0 + b * k * k) * t);
                return 0.15f * (float) s;
            });

            if (! reading.hasPitch() || orion::toNote (reading.frequency).midiNote != orion::toNote ((float) f).midiNote)
            {
                ++wrong;
                continue;
            }
            const float cents = centsBetween (reading.frequency, f * std::sqrt (1.0 + b));
            lowest = juce::jmin (lowest, cents);
            highest = juce::jmax (highest, cents);
        }

        const float limit = b < 2.0e-4 ? 2.0f : 8.0f;
        check (wrong == 0 && lowest > -1.0f && highest < limit && highest - lowest < 4.0f,
               "stiff string (B = " + juce::String (b, 4) + "): right note, " + juce::String (lowest, 1) + " to +"
                   + juce::String (highest, 1) + " cents from the fundamental");
    }

    check (! detect (detector, [] (int) { return 0.0f; }).hasPitch(), "silence: no note");
    check (! detect (detector, noiseBurst (48000.0, 10.0, 0.0001f), 0).hasPitch(), "-80 dBFS noise: no note");
    check (! detect (detector, noiseBurst (48000.0, 10.0, 0.3f), 0).hasPitch(), "loud white noise: no note");

    // Analysis cost at the highest rate (it runs 30 times a second on the UI thread).
    detector.prepare (96000.0);
    const auto tone = guitarTone (96000.0, 82.407, 0.5f);
    std::vector<float> samples ((size_t) detector.getRequiredInputSamples());
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = tone ((int) i);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 30; ++i)
        detector.analyse (samples.data());
    const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 30.0;
    check (ms < 5.0, "one analysis takes " + juce::String (ms, 2) + " ms at 96 kHz");
}

void testTunerInProcessor()
{
    std::cout << "\nTuner in the plugin: mutes, listens to the dry input, never saved\n";
    const double sr = 48000.0;
    const auto lowE = guitarTone (sr, 82.407, 0.5f);

    {
        OrionAudioProcessor p;
        const auto open = run (p, sr, 256, 1.0, lowE);
        p.setTunerActive (true);
        const auto muted = run (p, sr, 256, 1.0, lowE);
        check (open.rms (sr, 0.2, 1.0) > 0.01f && muted.finite && muted.peak < 1.0e-6f,
               "tuner on: output silent (peak " + juce::String (toDb (muted.peak), 1) + " dBFS)");

        // The tap carries the dry input, so the tuner reads the string even with drives on.
        orion::PitchDetector detector;
        detector.prepare (sr);
        std::vector<float> samples ((size_t) detector.getRequiredInputSamples());
        const bool copied = p.getInputTap().copyLatest (samples.data(), (int) samples.size());
        const auto reading = detector.analyse (samples.data());
        const auto note = orion::toNote (reading.frequency);
        check (copied && note.getName() == "E" && note.getOctave() == 2 && std::abs (note.cents) < 1.0f,
               "reads the input through the plugin: " + note.getName() + juce::String (note.getOctave()) + " "
                   + juce::String (note.cents, 2) + " cents");
    }

    {
        // Switching on mid-stream fades out rather than cutting.
        OrionAudioProcessor p;
        setAllPedals (p, false);
        int block = 0;
        const auto r = run (p, sr, 64, 0.5, [=] (int i) { return 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr); },
                            [&block] (OrionAudioProcessor& proc, juce::Random&) { if (++block == 200) proc.setTunerActive (true); });
        const auto at = (size_t) (199 * 64);
        float worstStep = 0.0f;
        for (size_t i = at; i < at + (size_t) (0.03 * sr); ++i)
            worstStep = juce::jmax (worstStep, std::abs (r.left[i] - r.left[i - 1]));
        check (worstStep < 0.05f && r.rms (sr, 0.4, 0.5) < 1.0e-6f, "fades out without a click (largest step " + juce::String (worstStep, 3) + ")");
    }

    {
        // Plucks played while tuning must not ring out of the echo and reverb afterwards.
        OrionAudioProcessor p;
        setAllPedals (p, false);
        setParam (p, "echo_on", 1.0f);
        setParam (p, "spring_on", 1.0f);
        setParam (p, "echo_repeats", 0.8f);
        p.setTunerActive (true);
        int block = 0;
        const int offBlock = (int) (1.0 * sr / 256);
        const auto r = run (p, sr, 256, 3.0, [&] (int i) { return i < (int) (0.9 * sr) ? lowE (i) : 0.0f; },
                            [&] (OrionAudioProcessor& proc, juce::Random&) { if (block++ == offBlock) proc.setTunerActive (false); });
        check (r.peak < 1.0e-6f, "tuning plucks don't ring out of echo / reverb when switched off (peak "
                                     + juce::String (toDb (r.peak), 1) + " dBFS)");
    }

    {
        OrionAudioProcessor a;
        a.setTunerActive (true);
        juce::MemoryBlock data;
        a.getStateInformation (data);
        OrionAudioProcessor b;
        b.setStateInformation (data.getData(), (int) data.getSize());
        const auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize());
        check (! b.isTunerActive() && xml != nullptr && ! xml->toString().containsIgnoreCase ("tuner"),
               "not saved in the session; a restored session isn't muted");
    }

    {
        OrionAudioProcessor p;
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor());
        p.setTunerActive (true);
        editor.reset();
        check (! p.isTunerActive(), "closing the editor switches the tuner off");
    }

    {
        // The header button and clicking the tuner display.
        OrionAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        juce::Button* button = nullptr;
        orion::TunerDisplay* display = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->getName() == "tuner")
                button = b;
            if (auto* d = dynamic_cast<orion::TunerDisplay*> (&c))
                display = d;
            for (auto* child : c.getChildren())
                find (*child);
        };
        find (*editor);

        const bool found = button != nullptr && display != nullptr;
        bool onWorks = false, dismissWorks = false;
        if (found)
        {
            button->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            onWorks = p.isTunerActive() && display->isVisible() && button->getToggleState();

            display->onDismiss();
            dismissWorks = ! p.isTunerActive() && ! display->isVisible() && ! button->getToggleState();
        }
        check (found && onWorks && dismissWorks, "TUNER button switches it on; clicking the display switches it off");
    }
}

} // namespace

static bool writeSnapshot (OrionAudioProcessor& p, const juce::File& file)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::cout << (ok ? "Wrote " : "Could not write ") << file.getFullPathName() << "\n";
    return ok;
}

static int snapshot (const juce::File& file)
{
    for (auto [name, style] : { std::pair { "Avenir Next", "Demi Bold" }, std::pair { "Avenir Next Condensed", "Medium" },
                                std::pair { "Snell Roundhand", "Bold" } })
    {
        juce::Font f (juce::FontOptions (name, style, 12.0f));
        auto tf = f.getTypefacePtr();
        std::cout << "Font '" << name << " / " << style << "' -> '"
                  << (tf != nullptr ? tf->getName() + " / " + tf->getStyle() : juce::String ("none")) << "'\n";
    }

    OrionAudioProcessor p;
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        std::cout << "Editor open + first paint at 2x (Retina): "
                  << juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) << " ms\n";

        const int w = editor->getWidth(), h = editor->getHeight();
        for (float s : { 1.6f, 0.6f })
        {
            editor->setSize ((int) ((float) w * s), (int) ((float) h * s));
            const auto stretched = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
            const auto t1 = juce::Time::getMillisecondCounterHiRes();
            const auto crisp = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            std::cout << "Resized to " << s << "x (" << editor->getWidth() << " x " << editor->getHeight()
                      << "), re-render after settling: " << juce::String (juce::Time::getMillisecondCounterHiRes() - t1, 0) << " ms\n";
            const auto out = file.getSiblingFile (file.getFileNameWithoutExtension() + "-" + juce::String (s, 1) + "x.png");
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat().writeImageToStream (crisp, stream);
            juce::ignoreUnused (stretched);
        }
    }

    // Stand-alone logo on a transparent background.
    {
        juce::Image logo (juce::Image::ARGB, 1200, 1200, true);
        juce::Graphics lg (logo);
        orion::drawOrionSparkMark (lg, { 600.0f, 600.0f }, 240.0f);
        const auto out = file.getSiblingFile ("logo.png");
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream (out);
        const bool logoOk = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (logo, stream);
        std::cout << (logoOk ? "Wrote " : "Could not write ") << out.getFullPathName() << "\n";

        // Preview on the plugin's dark background (image viewers often show alpha badly).
        juce::Image preview (juce::Image::RGB, 1200, 1200, true);
        juce::Graphics pg (preview);
        pg.fillAll (juce::Colour (0xff0b0b0c));
        pg.drawImageAt (logo, 0, 0);
        const auto previewFile = file.getSiblingFile ("logo-on-dark.png");
        previewFile.deleteFile();
        juce::FileOutputStream ps (previewFile);
        juce::PNGImageFormat().writeImageToStream (preview, ps);
    }

    bool ok = writeSnapshot (p, file);
    setParam (p, "echo_on", 0.0f);
    ok = writeSnapshot (p, file.getSiblingFile (file.getFileNameWithoutExtension() + "-echo-off.png")) && ok;

    // Tuner on, reading a low E string 4 cents sharp.
    {
        OrionAudioProcessor tp;
        run (tp, 48000.0, 256, 0.5, guitarTone (48000.0, 82.407 * std::pow (2.0, 4.0 / 1200.0), 0.5f));
        tp.setTunerActive (true);
        std::unique_ptr<juce::AudioProcessorEditor> editor (tp.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);   // let the display take a few readings
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto out = file.getSiblingFile (file.getFileNameWithoutExtension() + "-tuner.png");
        out.deleteFile();
        juce::FileOutputStream stream (out);
        const bool tunerOk = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << (tunerOk ? "Wrote " : "Could not write ") << out.getFullPathName() << "\n";
        ok = tunerOk && ok;
    }

    return ok ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc == 3 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File (argv[2]));

    testSilenceInSilenceOut();
    testBypassIsTransparent();
    testStereoInput();
    testWetPathLatency();
    reportLevels();
    testDefaultsAcrossRatesAndBlocks();
    testSweep();
    testWorstCaseSettings();
    testAutomationStress();
    testStateRoundTrip();
    testPresets();
    testPresetStateRoundTrip();
    testUserPresets();
    testCpu();
    testTunerNotes();
    testTunerAccuracy();
    testTunerInProcessor();

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
