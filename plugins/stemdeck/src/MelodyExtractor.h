#pragma once

#include <JuceHeader.h>
#include <vector>
#include <algorithm>

// =============================================================================
//  MelodyExtractor — monophonic audio→MIDI via YIN pitch tracking.
// -----------------------------------------------------------------------------
//  Offline, CPU-only. Intended to run on a BACKGROUND thread:
//
//      auto notes = MelodyExtractor::extract (loaded->audio, loaded->sourceSampleRate);
//
//  Pipeline: mixdown → decimate to ~22 kHz → YIN per frame (cumulative
//  mean-normalised difference, threshold + parabolic refinement) → RMS gate →
//  median-smoothed pitch track → segment into notes (≥60 ms, ±0.6 semitone
//  stability). Analysis is capped at 90 s. Polyphonic material yields the
//  dominant line, not chords — this is a melody tool by design.
// =============================================================================
namespace MelodyExtractor
{
    struct Note
    {
        int    pitch = 60;          // MIDI note number
        double startSeconds = 0.0;
        double lengthSeconds = 0.0;
        int    velocity = 100;      // 1..127 from frame RMS
    };

    namespace detail
    {
        inline float frameRms (const float* x, int n)
        {
            double acc = 0.0;
            for (int i = 0; i < n; ++i) acc += (double) x[i] * x[i];
            return (float) std::sqrt (acc / juce::jmax (1, n));
        }

        // YIN: returns f0 in Hz, or 0 when unvoiced.
        inline float yinPitch (const float* x, int w, double sr, int tauMin, int tauMax, float threshold)
        {
            const int maxTau = juce::jmin (tauMax, w - 1);
            if (maxTau <= tauMin + 2)
                return 0.0f;

            std::vector<float> d ((size_t) maxTau + 1, 0.0f);
            for (int tau = 1; tau <= maxTau; ++tau)
            {
                float acc = 0.0f;
                for (int i = 0; i + tau < w; ++i)
                {
                    const float diff = x[i] - x[i + tau];
                    acc += diff * diff;
                }
                d[(size_t) tau] = acc;
            }

            // Cumulative mean-normalised difference.
            std::vector<float> cmnd ((size_t) maxTau + 1, 1.0f);
            float running = 0.0f;
            for (int tau = 1; tau <= maxTau; ++tau)
            {
                running += d[(size_t) tau];
                cmnd[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
            }

            int tauEst = -1;
            for (int tau = tauMin; tau <= maxTau; ++tau)
            {
                if (cmnd[(size_t) tau] < threshold)
                {
                    while (tau + 1 <= maxTau && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                        ++tau;
                    tauEst = tau;
                    break;
                }
            }
            if (tauEst < 0)
                return 0.0f;

            // Parabolic interpolation around the minimum.
            double tauRefined = tauEst;
            if (tauEst > 1 && tauEst < maxTau)
            {
                const double a = cmnd[(size_t) (tauEst - 1)], b = cmnd[(size_t) tauEst], c = cmnd[(size_t) (tauEst + 1)];
                const double denom = a - 2.0 * b + c;
                if (std::abs (denom) > 1.0e-12)
                    tauRefined = tauEst + 0.5 * (a - c) / denom;
            }
            return tauRefined > 0.0 ? (float) (sr / tauRefined) : 0.0f;
        }
    }

    // Extract a monophonic note list from an audio buffer.
    inline std::vector<Note> extract (const juce::AudioBuffer<float>& source, double sourceSampleRate,
                                      double maxAnalysisSeconds = 90.0)
    {
        std::vector<Note> notes;
        if (source.getNumSamples() < 1024 || sourceSampleRate <= 0.0)
            return notes;

        // ── Mixdown + decimate to ~22 kHz ───────────────────────────────────
        const int decim = sourceSampleRate > 30000.0 ? 2 : 1;
        const double sr = sourceSampleRate / (double) decim;
        const int total = juce::jmin ((int) (maxAnalysisSeconds * sourceSampleRate),
                                      source.getNumSamples()) / decim;
        std::vector<float> mono ((size_t) total, 0.0f);
        {
            const int nCh = source.getNumChannels();
            for (int i = 0; i < total; ++i)
            {
                float acc = 0.0f;
                for (int c = 0; c < nCh; ++c)
                    for (int k = 0; k < decim; ++k)
                        acc += source.getSample (c, i * decim + k);
                mono[(size_t) i] = acc / (float) (nCh * decim);
            }
        }

        // ── Frame-wise YIN ──────────────────────────────────────────────────
        const int frame = 1024, hop = 256;
        const int tauMin = juce::jmax (2,  (int) (sr / 1000.0));   // ≤1 kHz
        const int tauMax = (int) (sr / 70.0);                      // ≥70 Hz
        const int nFrames = juce::jmax (0, (total - frame) / hop);
        if (nFrames < 4)
            return notes;

        std::vector<float> midiTrack ((size_t) nFrames, 0.0f);     // 0 = unvoiced
        std::vector<float> rmsTrack  ((size_t) nFrames, 0.0f);
        float peakRms = 1.0e-6f;

        for (int f = 0; f < nFrames; ++f)
        {
            const float* x = mono.data() + (size_t) (f * hop);
            const float rms = detail::frameRms (x, frame);
            rmsTrack[(size_t) f] = rms;
            peakRms = juce::jmax (peakRms, rms);

            const float f0 = detail::yinPitch (x, frame, sr, tauMin, tauMax, 0.15f);
            if (f0 > 0.0f)
                midiTrack[(size_t) f] = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        }

        // ── RMS gate + 3-point median smoothing ─────────────────────────────
        const float gate = peakRms * 0.06f;
        for (int f = 0; f < nFrames; ++f)
            if (rmsTrack[(size_t) f] < gate)
                midiTrack[(size_t) f] = 0.0f;

        std::vector<float> smooth = midiTrack;
        for (int f = 1; f + 1 < nFrames; ++f)
        {
            std::array<float, 3> w { midiTrack[(size_t) (f - 1)], midiTrack[(size_t) f], midiTrack[(size_t) (f + 1)] };
            std::sort (w.begin(), w.end());
            smooth[(size_t) f] = w[1];
        }

        // ── Segment into notes ──────────────────────────────────────────────
        const double frameDur = (double) hop / sr;
        const int minFrames = juce::jmax (2, (int) (0.060 / frameDur));   // ≥60 ms

        int segStart = -1;
        float segSum = 0.0f, segRms = 0.0f;
        int segCount = 0;

        auto flush = [&] (int endFrame)
        {
            if (segStart >= 0 && segCount >= minFrames)
            {
                const int pitch = juce::jlimit (12, 120, (int) std::round (segSum / (float) segCount));
                Note n;
                n.pitch         = pitch;
                n.startSeconds  = segStart * frameDur;
                n.lengthSeconds = (endFrame - segStart) * frameDur;
                n.velocity      = juce::jlimit (20, 127, (int) (127.0f * std::sqrt (segRms / (float) segCount / peakRms)));
                notes.push_back (n);
            }
            segStart = -1; segSum = 0.0f; segRms = 0.0f; segCount = 0;
        };

        for (int f = 0; f < nFrames; ++f)
        {
            const float m = smooth[(size_t) f];
            if (m <= 0.0f) { flush (f); continue; }

            if (segStart < 0)
            {
                segStart = f; segSum = m; segRms = rmsTrack[(size_t) f]; segCount = 1;
            }
            else if (std::abs (m - segSum / (float) segCount) > 0.6f)   // pitch break
            {
                flush (f);
                segStart = f; segSum = m; segRms = rmsTrack[(size_t) f]; segCount = 1;
            }
            else
            {
                segSum += m; segRms += rmsTrack[(size_t) f]; ++segCount;
            }
        }
        flush (nFrames);

        return notes;
    }
}
