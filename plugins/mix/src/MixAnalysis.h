#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariKWeighting.h>

#include <array>
#include <atomic>
#include <cmath>

namespace sattari::automix
{
struct Measurement
{
    float loudness = -100.0f, peakDb = -100.0f;
    float low = 0.0f, presence = 0.0f, crest = 0.0f;
    float activeSeconds = 0.0f, seconds = 0.0f, progress = 0.0f;
    bool complete = false, invalid = false;
};

// Fixed 100 ms frames, gated relative to the loudest frame after capture.
// This is an active K-weighted level estimate, not integrated BS.1770 LUFS.
class Analyzer
{
public:
    Analyzer() { publish ({}); }
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        frameLength = juce::jmax (1, juce::roundToInt (sr * 0.1));
        lowAlpha = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 180.0 / sr);
        presenceAlpha = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 2000.0 / sr);
        for (auto& f : shelf) dsp::designKWeightingShelf (f, sr);
        for (auto& f : highpass) dsp::designKWeightingHighPass (f, sr);
        reset (0);
    }

    void reset (int durationSeconds) noexcept
    {
        for (auto& f : shelf) f.reset();
        for (auto& f : highpass) f.reset();
        lowState.fill (0); presenceState.fill (0); frames.fill ({});
        frameSamples = frameCount = 0; current = {}; peak = 0; bad = false;
        targetFrames = juce::jlimit (0, 300, durationSeconds * 10);
        publish ({});
    }

    void process (const juce::AudioBuffer<float>& audio) noexcept
    {
        if (targetFrames == 0 || frameCount >= targetFrames) return;
        const int channels = juce::jmin (2, audio.getNumChannels());
        if (channels == 0) return;
        for (int i = 0; i < audio.getNumSamples() && frameCount < targetFrames; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                double x = audio.getSample (ch, i);
                if (! std::isfinite (x) || std::abs (x) > 64.0) { bad = true; x = 0; }
                peak = std::max (peak, std::abs (x));
                const auto c = (size_t) ch;
                const double k = highpass[c].process (shelf[c].process (x));
                lowState[c] += lowAlpha * (x - lowState[c]);
                presenceState[c] += presenceAlpha * (x - presenceState[c]);
                const double hi = x - presenceState[c];
                current.weighted += k * k;
                current.raw += x * x;
                current.low += lowState[c] * lowState[c];
                current.presence += hi * hi;
            }
            if (++frameSamples == frameLength)
            {
                const double scale = 1.0 / frameLength;
                current.weighted *= scale; current.raw *= scale;
                current.low *= scale; current.presence *= scale;
                frames[(size_t) frameCount++] = current;
                current = {}; frameSamples = 0;
            }
        }
        Measurement m;
        m.seconds = (float) frameCount * 0.1f;
        m.progress = (float) frameCount / (float) targetFrames;
        m.complete = frameCount == targetFrames;
        m.invalid = bad;
        m.peakDb = db (peak * peak);
        double highest = 0;
        for (int i = 0; i < frameCount; ++i) highest = std::max (highest, frames[(size_t) i].weighted);
        const double gate = std::max (1.0e-6, highest * 0.01);
        Frame total;
        int active = 0;
        for (int i = 0; i < frameCount; ++i)
        {
            const auto& f = frames[(size_t) i];
            if (f.weighted < gate) continue;
            total.weighted += f.weighted; total.raw += f.raw;
            total.low += f.low; total.presence += f.presence; ++active;
        }
        m.activeSeconds = (float) active * 0.1f;
        if (active > 0)
        {
            m.loudness = -0.691f + db (total.weighted / active);
            m.low = (float) juce::jlimit (0.0, 1.0, total.low / std::max (1.0e-12, total.raw));
            m.presence = (float) juce::jlimit (0.0, 1.0, total.presence / std::max (1.0e-12, total.raw));
            m.crest = juce::jlimit (0.0f, 40.0f, m.peakDb - db (total.raw / (active * channels)));
        }
        publish (m);
    }

    Measurement read() const noexcept
    {
        Measurement m;
        // Atomic fields avoid a C++ data race even when the reader retries.
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = revision.load();
            if ((before & 1u) != 0) continue;
            m.loudness = values[0].load(); m.peakDb = values[1].load();
            m.low = values[2].load(); m.presence = values[3].load(); m.crest = values[4].load();
            m.activeSeconds = values[5].load(); m.seconds = values[6].load();
            m.progress = values[7].load(); m.complete = values[8].load() > 0;
            m.invalid = values[9].load() > 0;
            if (before == revision.load()) return m;
        }
        return {}; // A busy producer is not a valid completed measurement.
    }

private:
    struct Frame { double weighted = 0, raw = 0, low = 0, presence = 0; };
    static float db (double power) noexcept { return power > 1.0e-10 ? (float) (10.0 * std::log10 (power)) : -100.0f; }
    void publish (const Measurement& m) noexcept
    {
        revision.fetch_add (1);
        values[0] = m.loudness; values[1] = m.peakDb; values[2] = m.low;
        values[3] = m.presence; values[4] = m.crest; values[5] = m.activeSeconds;
        values[6] = m.seconds; values[7] = m.progress;
        values[8] = m.complete ? 1.0f : 0.0f; values[9] = m.invalid ? 1.0f : 0.0f;
        revision.fetch_add (1);
    }
    double sr = 48000, lowAlpha = 0, presenceAlpha = 0, peak = 0;
    std::array<dsp::KBiquad, 2> shelf, highpass;
    std::array<double, 2> lowState {}, presenceState {};
    std::array<Frame, 300> frames {};
    Frame current;
    int frameLength = 4800, frameSamples = 0, frameCount = 0, targetFrames = 0;
    bool bad = false;
    std::atomic<unsigned> revision { 0 };
    std::array<std::atomic<float>, 10> values {};
};
} // namespace sattari::automix
