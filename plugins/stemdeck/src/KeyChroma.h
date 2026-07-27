#pragma once

// KeyChroma — self-contained chroma + Krumhansl-Schmuckler key detection for the
// §6.2 key-over-time curve. Deliberately independent of the processor's neural key
// model / analysis cache: it runs on the editor's peak-extraction worker over short
// windows to reveal WHERE a track modulates (the shape), while the neural model
// still owns the single most-accurate overall key. Goertzel (no FFT lib) so it only
// needs juce_audio_basics.
//
// Accuracy of the absolute key per window is audio-verified by the user; the pipeline
// (windowing, smoothing, modulation detection) is unit-tested in KeyCurve.h.

#include "KeyCurve.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace stemdeck
{

// Goertzel magnitude of one frequency over a mono-summed window.
inline double goertzelMag (const juce::AudioBuffer<float>& buf, int start, int len, double sr, double freq)
{
    if (len <= 0 || sr <= 0.0) return 0.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
    const double cw = std::cos (w), coeff = 2.0 * cw;
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    const int ch = buf.getNumChannels();
    for (int i = 0; i < len; ++i)
    {
        double x = 0.0;
        for (int c = 0; c < ch; ++c) x += buf.getSample (c, start + i);
        x /= juce::jmax (1, ch);
        s0 = x + coeff * s1 - s2;
        s2 = s1; s1 = s0;
    }
    return std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

// 12-bin chroma: sum Goertzel energy at each semitone across octaves C2..B6.
inline std::array<double, 12> chromaVector (const juce::AudioBuffer<float>& buf, int start, int len, double sr)
{
    std::array<double, 12> c {};
    for (int pc = 0; pc < 12; ++pc)
    {
        double e = 0.0;
        // MIDI 36 (C2) .. 95 (B6): note = 36 + octave*12 + pc.
        for (int octave = 0; octave < 5; ++octave)
        {
            const int midi = 36 + octave * 12 + pc;
            const double freq = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
            if (freq < sr * 0.5) e += goertzelMag (buf, start, len, sr, freq);
        }
        c[(size_t) pc] = e;
    }
    // Normalise (so correlation is scale-invariant).
    double mx = 0.0; for (double v : c) mx = juce::jmax (mx, v);
    if (mx > 0.0) for (double& v : c) v /= mx;
    return c;
}

// Krumhansl-Schmuckler major/minor tone profiles.
inline const std::array<double, 12>& krumhanslMajor()
{
    static const std::array<double, 12> p { 6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88 };
    return p;
}
inline const std::array<double, 12>& krumhanslMinor()
{
    static const std::array<double, 12> p { 6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17 };
    return p;
}

inline double pearson (const std::array<double, 12>& a, const std::array<double, 12>& b)
{
    double ma = 0, mb = 0; for (int i = 0; i < 12; ++i) { ma += a[(size_t) i]; mb += b[(size_t) i]; }
    ma /= 12; mb /= 12;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double xa = a[(size_t) i] - ma, xb = b[(size_t) i] - mb;
        num += xa * xb; da += xa * xa; db += xb * xb;
    }
    return (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0.0;
}

// Best key for a chroma vector. confidence = winning correlation minus the runner-up.
inline KeyObservation keyFromChroma (const std::array<double, 12>& c, double atBeat = 0.0)
{
    double best = -2.0, second = -2.0; int bestPc = 0; bool bestMinor = false;
    for (int minor = 0; minor <= 1; ++minor)
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            std::array<double, 12> prof {};
            const auto& base = minor ? krumhanslMinor() : krumhanslMajor();
            for (int i = 0; i < 12; ++i) prof[(size_t) i] = base[(size_t) ((i - tonic + 12) % 12)];
            const double r = pearson (c, prof);
            if (r > best) { second = best; best = r; bestPc = tonic; bestMinor = minor == 1; }
            else if (r > second) second = r;
        }
    KeyObservation o;
    o.atBeat = atBeat; o.pitchClass = bestPc; o.minor = bestMinor;
    o.confidence = (float) juce::jlimit (0.0, 1.0, best - juce::jmax (0.0, second));
    return o;
}

inline KeyObservation detectKeyChroma (const juce::AudioBuffer<float>& buf, int start, int len, double sr, double atBeat = 0.0)
{
    return keyFromChroma (chromaVector (buf, start, len, sr), atBeat);
}

// Full key-over-time curve for a track: window, detect per window, smooth. Positions
// are stored NORMALISED (0..1) in atBeat for the overview visualisation.
inline std::vector<KeyObservation> analyzeKeyCurve (const juce::AudioBuffer<float>& buf, double sr,
                                                    int windows = 12)
{
    std::vector<KeyObservation> raw;
    const int n = buf.getNumSamples();
    if (n <= 0 || sr <= 0.0) return raw;
    const int win = juce::jmax (1, n / juce::jmax (1, windows));
    for (int w = 0; w < windows; ++w)
    {
        const int s0 = w * win;
        if (s0 >= n) break;
        const int len = juce::jmin (win, n - s0);
        raw.push_back (detectKeyChroma (buf, s0, len, sr, (double) w / (double) windows));
    }
    return smoothKeyCurve (std::move (raw), 3);
}

} // namespace stemdeck
