#pragma once

#include <JuceHeader.h>
#include <vector>
#include "MelodyExtractor.h"   // reuses the Note struct (pitch = GM drum number)

// =============================================================================
//  DrumExtractor — beatboxing / percussion → drum pattern (GM drum MIDI).
// -----------------------------------------------------------------------------
//  Pure DSP, honest about it: onset detection by band-energy flux, then each
//  onset is classified kick / snare / hat from its low/mid/high energy split.
//
//    kick  → 36 (C1)   low-band dominant
//    snare → 38 (D1)   broadband with strong mid
//    hat   → 42 (F#1)  high-band dominant
//
//  Runs offline on a background thread; analysis capped at 60 s.
// =============================================================================
namespace DrumExtractor
{
    inline std::vector<MelodyExtractor::Note> extract (const juce::AudioBuffer<float>& source,
                                                       double sourceSampleRate,
                                                       double maxAnalysisSeconds = 60.0)
    {
        std::vector<MelodyExtractor::Note> hits;
        if (source.getNumSamples() < 1024 || sourceSampleRate <= 0.0)
            return hits;

        // ── Mixdown (no decimation: hats need the top octaves) ──────────────
        const double sr = sourceSampleRate;
        const int total = juce::jmin ((int) (maxAnalysisSeconds * sr), source.getNumSamples());
        std::vector<float> mono ((size_t) total, 0.0f);
        {
            const int nCh = source.getNumChannels();
            for (int i = 0; i < total; ++i)
            {
                float acc = 0.0f;
                for (int c = 0; c < nCh; ++c)
                    acc += source.getSample (c, i);
                mono[(size_t) i] = acc / (float) nCh;
            }
        }

        // ── Frame-wise 3-band energies (one-pole crossovers at 200 Hz / 2 kHz) ─
        const int frame = 512, hop = 256;                      // ~11.6 ms / 5.8 ms @44.1k
        const int nFrames = juce::jmax (0, (total - frame) / hop);
        if (nFrames < 4)
            return hits;

        const float lpC = juce::jlimit (0.0001f, 0.9999f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 200.0f  / (float) sr));
        const float hpC = juce::jlimit (0.0001f, 0.9999f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 2000.0f / (float) sr));

        // Filter the whole (contiguous) signal ONCE — the one-pole state must run
        // over consecutive samples. Running it per overlapping frame re-processes
        // the 50%-overlap region with stale state and corrupts the band energies.
        std::vector<float> loBand ((size_t) total), hiBand ((size_t) total);
        {
            float lpState = 0.0f, hpState = 0.0f;
            for (int i = 0; i < total; ++i)
            {
                lpState += lpC * (mono[(size_t) i] - lpState);   // <200 Hz
                hpState += hpC * (mono[(size_t) i] - hpState);   // (lp for subtraction)
                loBand[(size_t) i] = lpState;
                hiBand[(size_t) i] = mono[(size_t) i] - hpState; // >2 kHz
            }
        }

        std::vector<float> lowE ((size_t) nFrames), midE ((size_t) nFrames), highE ((size_t) nFrames), totE ((size_t) nFrames);
        for (int f = 0; f < nFrames; ++f)
        {
            const int base = f * hop;
            float lo = 0.0f, hi = 0.0f, tot = 0.0f;
            for (int i = 0; i < frame; ++i)
            {
                const float l = loBand[(size_t) (base + i)];
                const float h = hiBand[(size_t) (base + i)];
                const float s = mono  [(size_t) (base + i)];
                lo  += l * l;
                hi  += h * h;
                tot += s * s;
            }
            lowE [(size_t) f] = lo  / (float) frame;
            highE[(size_t) f] = hi  / (float) frame;
            totE [(size_t) f] = tot / (float) frame;
            midE [(size_t) f] = juce::jmax (0.0f, totE[(size_t) f] - lowE[(size_t) f] - highE[(size_t) f]);
        }

        // ── Onset detection: positive total-energy flux above adaptive floor ─
        float peakTot = 1.0e-9f;
        for (int f = 0; f < nFrames; ++f) peakTot = juce::jmax (peakTot, totE[(size_t) f]);
        const float floorGate = peakTot * 0.02f;

        const double frameDur = (double) hop / sr;
        const int refractory = juce::jmax (1, (int) (0.075 / frameDur));   // 75 ms between hits
        int lastOnset = -refractory;

        for (int f = 1; f < nFrames; ++f)
        {
            const float flux = totE[(size_t) f] - totE[(size_t) (f - 1)];
            if (flux < peakTot * 0.05f || totE[(size_t) f] < floorGate || f - lastOnset < refractory)
                continue;
            lastOnset = f;

            // Classify from the energy split over the next couple of frames.
            const int f2 = juce::jmin (nFrames - 1, f + 1);
            const float lo = lowE[(size_t) f] + lowE[(size_t) f2];
            const float mi = midE[(size_t) f] + midE[(size_t) f2];
            const float hi = highE[(size_t) f] + highE[(size_t) f2];
            const float sum = lo + mi + hi + 1.0e-12f;

            int pitch = 38;                                    // default snare
            if (lo / sum > 0.55f)                pitch = 36;   // kick: low-dominant
            else if (hi / sum > 0.55f)           pitch = 42;   // hat: high-dominant
            else if (lo / sum > 0.30f && hi / sum < 0.25f)
                                                 pitch = 36;   // boomy kick with body

            MelodyExtractor::Note n;
            n.pitch         = pitch;
            n.startSeconds  = f * frameDur;
            n.lengthSeconds = 0.10;
            n.velocity      = juce::jlimit (30, 127, (int) (127.0f * std::sqrt (totE[(size_t) f] / peakTot)));
            hits.push_back (n);
        }
        return hits;
    }
}
