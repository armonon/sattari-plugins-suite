#pragma once

// =============================================================================
//  Sattari Audio Suite — offline render + null-test harness
// -----------------------------------------------------------------------------
//  Turns "I think it still sounds right" into a fact. A render driver:
//    1. Synthesises a DETERMINISTIC test signal (same bytes every run).
//    2. Renders it through a plugin processor offline.
//    3. Writes a .wav you can actually listen to (validation/renders/).
//    4. Computes a compact fingerprint (per-segment RMS + global metrics).
//    5. Null-tests that fingerprint against a committed golden file: if the DSP
//       output drifts between commits, the residual spikes and the gate fails.
//
//  This is the regression net the synthetic-signal contracts cannot give: the
//  contracts prove "didn't crash + correct on a sine", this proves "sounds the
//  same as the last blessed render". Re-bless intentionally with --bless when a
//  sound change is wanted.
//
//  Pure JUCE + std; no realtime constraints (offline).
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace sattari::harness
{

// --- Deterministic test signals ---------------------------------------------

// A vocal-like mono tone: a pitch glide with vibrato, a few decaying harmonics,
// a soft amplitude envelope, and a touch of deterministic breath noise. Good
// stress material for pitch correction / formant / detection paths.
inline void generateVocalLike (juce::AudioBuffer<float>& buf, double sr, double seconds)
{
    const int n = (int) std::round (seconds * sr);
    buf.setSize (1, n, false, false, true);
    float* d = buf.getWritePointer (0);

    // Deterministic LCG so the "noise" is reproducible across runs/machines.
    juce::uint32 rng = 0x9E3779B9u;
    auto noise = [&rng]() {
        rng = rng * 1664525u + 1013904223u;
        return ((float) rng / (float) 0xFFFFFFFFu) * 2.0f - 1.0f;
    };

    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        // Pitch glide A3 (220) -> ~C4 (262) with 5 Hz, ±25-cent vibrato.
        const double base = 220.0 + (262.0 - 220.0) * (t / seconds);
        const double vib  = std::pow (2.0, (0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t)) / 12.0);
        const double f0   = base * vib;
        phase += 2.0 * juce::MathConstants<double>::pi * f0 / sr;

        double s = 0.0;
        s += 1.00 * std::sin (phase);
        s += 0.45 * std::sin (2.0 * phase);
        s += 0.25 * std::sin (3.0 * phase);
        s += 0.12 * std::sin (4.0 * phase);
        s += 0.05 * (double) noise();                 // breath

        // Slow tremolo amplitude envelope, never fully silent.
        const double env = 0.55 + 0.35 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.7 * t);
        d[i] = (float) (0.20 * env * s);
    }
}

// A click train + steady tone for transient/duck/align style tests.
inline void generateClickAndTone (juce::AudioBuffer<float>& buf, double sr, double seconds,
                                  double clickHz = 2.0, double toneHz = 220.0)
{
    const int n = (int) std::round (seconds * sr);
    buf.setSize (1, n, false, false, true);
    float* d = buf.getWritePointer (0);
    const int period = juce::jmax (1, (int) std::round (sr / clickHz));
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * juce::MathConstants<double>::pi * toneHz / sr;
        const float tone  = 0.15f * (float) std::sin (phase);
        const float click = (i % period < 16) ? 0.7f : 0.0f;
        d[i] = tone + click;
    }
}

// --- Rendering ---------------------------------------------------------------

// Render `input` (mono or stereo) through a prepared processor in blocks.
// Returns the output as a stereo buffer (duplicating mono input to L/R first).
inline juce::AudioBuffer<float> renderThrough (juce::AudioProcessor& proc,
                                               const juce::AudioBuffer<float>& input,
                                               double sr, int blockSize)
{
    proc.setRateAndBufferSizeDetails (sr, blockSize);
    proc.prepareToPlay (sr, blockSize);

    const int total = input.getNumSamples();
    juce::AudioBuffer<float> out (2, total);
    out.clear();

    juce::AudioBuffer<float> block (2, blockSize);
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int len = juce::jmin (blockSize, total - pos);
        block.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            const int srcCh = juce::jmin (ch, input.getNumChannels() - 1);
            block.copyFrom (ch, 0, input, srcCh, pos, len);
        }
        juce::AudioBuffer<float> slice (block.getArrayOfWritePointers(), 2, len);
        juce::MidiBuffer midi;
        proc.processBlock (slice, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, slice, ch, 0, len);
    }
    return out;
}

inline bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) return false;
    const auto options = juce::AudioFormatWriterOptions {}
        .withSampleRate (sr)
        .withNumChannels (buf.getNumChannels())
        .withBitsPerSample (24);
    std::unique_ptr<juce::OutputStream> output (stream.release());
    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (output, options));
    if (writer == nullptr) return false;
    return writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

// --- Fingerprint + null-test -------------------------------------------------

struct Fingerprint
{
    std::vector<float> segmentRms;   // N equal segments, mono-summed RMS
    float peak = 0.0f;
    float rms  = 0.0f;
    float centroidHz = 0.0f;         // crude spectral centroid proxy (zero-cross based)
};

inline Fingerprint fingerprint (const juce::AudioBuffer<float>& buf, double sr, int segments = 64)
{
    Fingerprint fp;
    const int n = buf.getNumSamples();
    const int ch = buf.getNumChannels();
    fp.segmentRms.assign ((size_t) segments, 0.0f);
    if (n == 0) return fp;

    double total = 0.0; float peak = 0.0f; long zc = 0; float prev = 0.0f;
    std::vector<double> segAcc ((size_t) segments, 0.0);
    std::vector<int>    segCnt ((size_t) segments, 0);
    for (int i = 0; i < n; ++i)
    {
        float m = 0.0f;
        for (int c = 0; c < ch; ++c) m += buf.getSample (c, i);
        m /= (float) ch;
        total += (double) m * m;
        peak = juce::jmax (peak, std::abs (m));
        if ((m >= 0.0f) != (prev >= 0.0f)) ++zc;
        prev = m;
        const int seg = juce::jlimit (0, segments - 1, (int) ((long long) i * segments / n));
        segAcc[(size_t) seg] += (double) m * m;
        segCnt[(size_t) seg] += 1;
    }
    for (int s = 0; s < segments; ++s)
        fp.segmentRms[(size_t) s] = (float) std::sqrt (segAcc[(size_t) s] / juce::jmax (1, segCnt[(size_t) s]));
    fp.peak = peak;
    fp.rms  = (float) std::sqrt (total / n);
    fp.centroidHz = (float) (0.5 * zc / ((double) n / sr));   // ~ dominant frequency proxy
    return fp;
}

inline juce::String fingerprintToText (const Fingerprint& fp)
{
    juce::String s;
    s << "peak " << juce::String (fp.peak, 6) << "\n";
    s << "rms " << juce::String (fp.rms, 6) << "\n";
    s << "centroidHz " << juce::String (fp.centroidHz, 2) << "\n";
    s << "segments " << (int) fp.segmentRms.size() << "\n";
    for (auto v : fp.segmentRms) s << juce::String (v, 6) << " ";
    s << "\n";
    return s;
}

inline Fingerprint fingerprintFromText (const juce::String& text)
{
    Fingerprint fp;
    auto lines = juce::StringArray::fromLines (text);
    for (auto& l : lines)
    {
        if (l.startsWith ("peak ")) fp.peak = l.fromFirstOccurrenceOf (" ", false, false).getFloatValue();
        else if (l.startsWith ("rms ")) fp.rms = l.fromFirstOccurrenceOf (" ", false, false).getFloatValue();
        else if (l.startsWith ("centroidHz ")) fp.centroidHz = l.fromFirstOccurrenceOf (" ", false, false).getFloatValue();
        else if (l.containsOnly ("0123456789.eE-+ ") && l.trim().isNotEmpty() && l.contains (" "))
        {
            auto toks = juce::StringArray::fromTokens (l.trim(), " ", "");
            for (auto& t : toks) if (t.isNotEmpty()) fp.segmentRms.push_back (t.getFloatValue());
        }
    }
    return fp;
}

// Max absolute residual between two fingerprints (segment RMS + globals).
inline float maxResidual (const Fingerprint& a, const Fingerprint& b)
{
    float r = juce::jmax (std::abs (a.peak - b.peak), std::abs (a.rms - b.rms));
    const size_t n = juce::jmin (a.segmentRms.size(), b.segmentRms.size());
    for (size_t i = 0; i < n; ++i)
        r = juce::jmax (r, std::abs (a.segmentRms[i] - b.segmentRms[i]));
    if (a.segmentRms.size() != b.segmentRms.size()) r = juce::jmax (r, 1.0f);  // shape changed
    return r;
}

} // namespace sattari::harness
