#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

/**
 * Sample-accurate arrangement playback engine.
 *
 * Plays a flat schedule of clips — each an in-memory audio buffer placed at a
 * timeline position with a source offset, gain, and fades — mixing them into the
 * master output. Clips are free-positioning: timelineStart is independent of the
 * source region, so the same recording can be cut, moved, and replayed.
 *
 * RT-safety model: the schedule is immutable while playing. setSchedule() is
 * only honoured when stopped, so the audio thread never races a swap and never
 * frees a schedule (the member always holds a reference; the audio thread only
 * copies the shared_ptr into a block-local under a brief, uncontended SpinLock).
 * Linear-interpolated reads handle any source/engine sample-rate mismatch with
 * no pre-resample allocation.
 */
class ArrangementPlayer
{
public:
    struct Clip
    {
        std::shared_ptr<const juce::AudioBuffer<float>> audio;
        double      sourceSampleRate     = 44100.0;
        juce::int64 timelineStartSamples = 0;   // engine sample-rate
        juce::int64 sourceStartSamples   = 0;   // source sample-rate
        juce::int64 timelineLengthSamples = 0;  // engine sample-rate
        float       gain = 1.0f;
        juce::int64 fadeInSamples  = 0;         // engine sample-rate
        juce::int64 fadeOutSamples = 0;         // engine sample-rate
    };

    using Schedule = std::vector<Clip>;

    void prepare (double sampleRate, int /*blockSize*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    }

    // Message thread, only meaningful while stopped. Computes total length.
    void setSchedule (std::shared_ptr<const Schedule> newSchedule)
    {
        juce::int64 len = 0;
        if (newSchedule != nullptr)
            for (const auto& c : *newSchedule)
                len = juce::jmax (len, c.timelineStartSamples + c.timelineLengthSamples);

        const juce::SpinLock::ScopedLockType l (lock);
        active = std::move (newSchedule);
        lengthSamples.store (len, std::memory_order_relaxed);
    }

    void start()                       { playing.store (true,  std::memory_order_relaxed); }
    void stop()                        { playing.store (false, std::memory_order_relaxed); }
    bool isPlaying() const noexcept    { return playing.load (std::memory_order_relaxed); }

    void setPositionSeconds (double s) { playhead.store ((juce::int64) (juce::jmax (0.0, s) * sr), std::memory_order_relaxed); }
    void setLoop (double startSec, double endSec, bool enabled)
    {
        loopStartSamples.store ((juce::int64) (juce::jmax (0.0, startSec) * sr), std::memory_order_relaxed);
        loopEndSamples  .store ((juce::int64) (juce::jmax (0.0, endSec)   * sr), std::memory_order_relaxed);
        loopEnabled.store (enabled, std::memory_order_relaxed);
    }
    double getPositionSeconds() const noexcept { return (double) playhead.load (std::memory_order_relaxed) / sr; }
    double getLengthSeconds()   const noexcept { return (double) lengthSamples.load (std::memory_order_relaxed) / sr; }

    // Audio thread. Adds the playing clips into @p out at the current playhead.
    void process (juce::AudioBuffer<float>& out)
    {
        if (! playing.load (std::memory_order_relaxed))
            return;

        std::shared_ptr<const Schedule> sched;
        {
            const juce::SpinLock::ScopedLockType l (lock);
            sched = active;   // member keeps a ref alive; this local is never the last owner
        }
        if (sched == nullptr || sched->empty())
            return;

        const int n = out.getNumSamples();
        const int outCh = out.getNumChannels();
        juce::int64 pos = playhead.load (std::memory_order_relaxed);
        const juce::int64 len = lengthSamples.load (std::memory_order_relaxed);

        for (const auto& clip : *sched)
        {
            if (clip.audio == nullptr || clip.timelineLengthSamples <= 0) continue;
            const juce::int64 clipEnd = clip.timelineStartSamples + clip.timelineLengthSamples;
            if (clipEnd <= pos || clip.timelineStartSamples >= pos + n) continue;   // no overlap with this block

            const auto& src = *clip.audio;
            const int srcCh = juce::jmax (1, src.getNumChannels());
            const int srcLen = src.getNumSamples();
            if (srcLen <= 1) continue;
            const double ratio = clip.sourceSampleRate / sr;

            const juce::int64 from = juce::jmax (pos, clip.timelineStartSamples);
            const juce::int64 to   = juce::jmin (pos + n, clipEnd);

            for (juce::int64 t = from; t < to; ++t)
            {
                const juce::int64 since = t - clip.timelineStartSamples;
                const double srcPos = (double) clip.sourceStartSamples + (double) since * ratio;

                float g = clip.gain;
                if (clip.fadeInSamples > 0 && since < clip.fadeInSamples)
                    g *= (float) since / (float) clip.fadeInSamples;
                if (clip.fadeOutSamples > 0 && since > clip.timelineLengthSamples - clip.fadeOutSamples)
                    g *= juce::jlimit (0.0f, 1.0f, (float) (clip.timelineLengthSamples - since) / (float) clip.fadeOutSamples);

                const int i = (int) (t - pos);
                for (int ch = 0; ch < outCh; ++ch)
                {
                    const float s = readInterpolated (src, juce::jmin (ch, srcCh - 1), srcPos);
                    out.addSample (ch, i, s * g);
                }
            }
        }

        pos += n;
        if (loopEnabled.load (std::memory_order_relaxed))
        {
            const juce::int64 ls = loopStartSamples.load (std::memory_order_relaxed);
            const juce::int64 le = loopEndSamples.load (std::memory_order_relaxed);
            if (le > ls && pos >= le)
                pos = ls;                                       // wrap at the loop end (block-boundary)
        }
        else if (len > 0 && pos >= len)
        {
            pos = len;
            playing.store (false, std::memory_order_relaxed);   // stop at the end
        }
        playhead.store (pos, std::memory_order_relaxed);
    }

private:
    static float readInterpolated (const juce::AudioBuffer<float>& buf, int ch, double pos)
    {
        const int n = buf.getNumSamples();
        if (pos < 0.0 || pos >= (double) (n - 1)) return 0.0f;
        const int i0 = (int) pos;
        const float frac = (float) (pos - (double) i0);
        const float* d = buf.getReadPointer (ch);
        return d[i0] + frac * (d[i0 + 1] - d[i0]);
    }

    double sr = 44100.0;
    juce::SpinLock lock;
    std::shared_ptr<const Schedule> active;
    std::atomic<bool>        playing { false };
    std::atomic<juce::int64> playhead { 0 };
    std::atomic<juce::int64> lengthSamples { 0 };
    std::atomic<bool>        loopEnabled { false };
    std::atomic<juce::int64> loopStartSamples { 0 };
    std::atomic<juce::int64> loopEndSamples { 0 };
};
