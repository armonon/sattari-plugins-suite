#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cmath>

/**
 * Simple transport-synced metronome: emits a short enveloped sine click on every
 * beat (an accented, higher click on each bar downbeat). Driven from the audio
 * thread with the arrangement playhead.
 */
class Metronome
{
public:
    void prepare (double sampleRate) { sr = sampleRate > 0.0 ? sampleRate : 44100.0; clickRemaining = 0; }
    void setEnabled (bool e) noexcept { enabled.store (e, std::memory_order_relaxed); }
    bool isEnabled() const noexcept   { return enabled.load (std::memory_order_relaxed); }
    void setBpm (double b) noexcept   { bpm.store (b > 0.0 ? b : 120.0, std::memory_order_relaxed); }
    void reset() noexcept             { clickRemaining = 0; }

    // Audio thread. @p pos is the block's start sample on the arrangement timeline.
    void process (juce::AudioBuffer<float>& out, juce::int64 pos, int n)
    {
        if (! enabled.load (std::memory_order_relaxed)) { clickRemaining = 0; return; }
        const double spb = 60.0 / bpm.load (std::memory_order_relaxed) * sr;
        if (spb <= 1.0) return;

        // The first beat boundary at or after pos, if it lands in this block.
        const juce::int64 firstBeat = (juce::int64) std::ceil ((double) pos / spb - 1.0e-6);
        const juce::int64 beatSample = (juce::int64) std::llround ((double) firstBeat * spb);
        const int startOffset = (beatSample >= pos && beatSample < pos + n) ? (int) (beatSample - pos) : -1;
        const bool accent = (firstBeat % 4) == 0;

        const int clickLen = (int) (0.035 * sr);
        for (int i = 0; i < n; ++i)
        {
            if (i == startOffset)
            {
                clickRemaining = clickLen;
                clickPhase = 0.0;
                clickFreq = accent ? 1568.0 : 880.0;   // G6 downbeat / A5 beat
            }
            if (clickRemaining > 0)
            {
                const float env = (float) clickRemaining / (float) juce::jmax (1, clickLen);
                const float s = (float) std::sin (juce::MathConstants<double>::twoPi * clickPhase) * 0.25f * env;
                for (int ch = out.getNumChannels(); --ch >= 0;)
                    out.addSample (ch, i, s);
                clickPhase += clickFreq / sr;
                if (clickPhase >= 1.0) clickPhase -= 1.0;
                --clickRemaining;
            }
        }
    }

private:
    double sr = 44100.0;
    std::atomic<bool>   enabled { false };
    std::atomic<double> bpm { 120.0 };
    int    clickRemaining = 0;
    double clickPhase = 0.0, clickFreq = 880.0;
};
