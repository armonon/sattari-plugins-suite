#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>
#include "ChunkedAudioFile.h"
#include "EQProcessor.h"
#include "FilterProcessor.h"
#include "DeckFX.h"

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
        // Long recordings use a bounded, read-ahead disk window instead of loading
        // the complete take into RAM. Exactly one of audio/streamed is normally set.
        std::shared_ptr<ChunkedAudioFile> streamed;
        double      sourceSampleRate     = 44100.0;
        juce::int64 timelineStartSamples = 0;   // engine sample-rate
        juce::int64 sourceStartSamples   = 0;   // source sample-rate
        juce::int64 timelineLengthSamples = 0;  // engine sample-rate
        float       gain = 1.0f;
        juce::int64 fadeInSamples  = 0;         // engine sample-rate
        juce::int64 fadeOutSamples = 0;         // engine sample-rate
        std::vector<std::pair<juce::int64, float>> gainAutomation;
        std::vector<std::pair<juce::int64, float>> lowAutomation;
        std::vector<std::pair<juce::int64, float>> midAutomation;
        std::vector<std::pair<juce::int64, float>> highAutomation;
        std::vector<std::pair<juce::int64, float>> filterAutomation;
        std::vector<std::pair<juce::int64, float>> reverbAutomation;
        std::vector<std::pair<juce::int64, float>> echoAutomation;
        bool dspEnabled = false;
        // Schedule ownership is immutable, but these are audio-thread-only render
        // histories. mutable permits stateful DSP without mutating clip placement.
        mutable EQProcessor eq;
        mutable FilterProcessor filter;
        mutable std::unique_ptr<DeckFX> deckFx;
    };

    using Schedule = std::vector<Clip>;

    struct MasterAutomation
    {
        std::vector<std::pair<juce::int64, float>> gainDb;
        std::vector<std::pair<juce::int64, float>> fxX;
        std::vector<std::pair<juce::int64, float>> fxY;
        std::vector<std::pair<juce::int64, float>> smartAmount;
        std::vector<std::pair<juce::int64, float>> smartMode;
        std::vector<std::pair<juce::int64, float>> smartOn;
        std::vector<std::pair<juce::int64, float>> limiterCeilingDb;

        bool empty() const noexcept
        {
            return gainDb.empty() && fxX.empty() && fxY.empty()
                && smartAmount.empty() && smartMode.empty() && smartOn.empty()
                && limiterCeilingDb.empty();
        }
    };

    struct MasterState
    {
        enum Presence : uint32_t
        {
            gainPresent        = 1u << 0,
            fxXPresent         = 1u << 1,
            fxYPresent         = 1u << 2,
            smartAmountPresent = 1u << 3,
            smartModePresent   = 1u << 4,
            smartOnPresent     = 1u << 5,
            limiterPresent     = 1u << 6
        };

        uint32_t presence = 0;
        float gainDb = 0.0f, fxX = 0.0f, fxY = 0.0f;
        float smartAmount = 0.0f, smartMode = 0.0f, smartOn = 0.0f;
        float limiterCeilingDb = -1.0f;

        bool has (Presence flag) const noexcept
        {
            return (presence & (uint32_t) flag) != 0;
        }
    };

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

    void setMasterAutomation (
        std::shared_ptr<const MasterAutomation> automation)
    {
        const juce::SpinLock::ScopedLockType l (lock);
        masterAutomation = std::move (automation);
        currentMasterState = {};
    }

    void start()
    {
        resetClipDsp();
        currentMasterState = {};
        playing.store (true, std::memory_order_relaxed);
    }
    void stop()                        { playing.store (false, std::memory_order_relaxed); }
    bool isPlaying() const noexcept    { return playing.load (std::memory_order_relaxed); }

    void setPositionSeconds (double s)
    {
        playhead.store ((juce::int64) (juce::jmax (0.0, s) * sr),
                        std::memory_order_relaxed);
        resetClipDsp();
    }
    void setLoop (double startSec, double endSec, bool enabled)
    {
        loopStartSamples.store ((juce::int64) (juce::jmax (0.0, startSec) * sr), std::memory_order_relaxed);
        loopEndSamples  .store ((juce::int64) (juce::jmax (0.0, endSec)   * sr), std::memory_order_relaxed);
        loopEnabled.store (enabled, std::memory_order_relaxed);
    }
    double getPositionSeconds() const noexcept { return (double) playhead.load (std::memory_order_relaxed) / sr; }
    double getLengthSeconds()   const noexcept { return (double) lengthSamples.load (std::memory_order_relaxed) / sr; }
    juce::int64 getPositionSamples() const noexcept { return playhead.load (std::memory_order_relaxed); }
    MasterState getCurrentMasterState() const noexcept
    {
        // Called immediately after process() from the same audio thread.
        return currentMasterState;
    }
    void setLengthSeconds (double s)   // raise the length to at least @p s (e.g. MIDI past the audio)
    {
        const juce::int64 want = (juce::int64) (juce::jmax (0.0, s) * sr);
        if (want > lengthSamples.load (std::memory_order_relaxed))
            lengthSamples.store (want, std::memory_order_relaxed);
    }

    // Audio thread. Adds the playing clips into @p out at the current playhead.
    void process (juce::AudioBuffer<float>& out)
    {
        if (! playing.load (std::memory_order_relaxed))
            return;

        std::shared_ptr<const Schedule> sched;
        std::shared_ptr<const MasterAutomation> master;
        {
            const juce::SpinLock::ScopedLockType l (lock);
            sched = active;   // member keeps a ref alive; this local is never the last owner
            master = masterAutomation;
        }
        const int n = out.getNumSamples();
        const int outCh = out.getNumChannels();
        juce::int64 pos = playhead.load (std::memory_order_relaxed);
        const juce::int64 len = lengthSamples.load (std::memory_order_relaxed);
        currentMasterState = master != nullptr
                           ? masterStateAt (*master, pos) : MasterState {};

        // Mix audio clips (skipped for a MIDI-only arrangement, but the transport
        // below still advances so the synth and playhead progress).
        if (sched != nullptr && ! sched->empty())
        for (const auto& clip : *sched)
        {
            if ((clip.audio == nullptr && clip.streamed == nullptr)
                || clip.timelineLengthSamples <= 0) continue;
            const juce::int64 clipEnd = clip.timelineStartSamples + clip.timelineLengthSamples;
            if (clipEnd <= pos || clip.timelineStartSamples >= pos + n) continue;   // no overlap with this block

            const int srcCh = clip.streamed != nullptr
                                  ? clip.streamed->numChannels()
                                  : juce::jmax (1, clip.audio->getNumChannels());
            const juce::int64 srcLen = clip.streamed != nullptr
                                           ? clip.streamed->lengthInSamples()
                                           : clip.audio->getNumSamples();
            if (srcLen <= 1) continue;
            const double ratio = clip.sourceSampleRate / sr;

            const juce::int64 from = juce::jmax (pos, clip.timelineStartSamples);
            const juce::int64 to   = juce::jmin (pos + n, clipEnd);
            const double firstSourcePos =
                (double) clip.sourceStartSamples
                + (double) (from - clip.timelineStartSamples) * ratio;
            const auto streamView = clip.streamed != nullptr
                                        ? clip.streamed->acquireReadView (
                                              firstSourcePos)
                                        : ChunkedAudioFile::ReadView {};

            for (juce::int64 t = from; t < to; ++t)
            {
                const juce::int64 since = t - clip.timelineStartSamples;
                const double srcPos = (double) clip.sourceStartSamples + (double) since * ratio;

                float left = clip.streamed != nullptr
                    ? ChunkedAudioFile::readInterpolated (
                          streamView, 0, srcPos)
                    : readInterpolated (*clip.audio, 0, srcPos);
                float right = srcCh > 1
                    ? (clip.streamed != nullptr
                        ? ChunkedAudioFile::readInterpolated (
                              streamView, 1, srcPos)
                        : readInterpolated (*clip.audio, 1, srcPos))
                    : left;

                if (clip.dspEnabled)
                {
                    clip.eq.processSample (
                        left, right,
                        automationValueAt (
                            clip.lowAutomation, t, 0.0f),
                        automationValueAt (
                            clip.midAutomation, t, 0.0f),
                        automationValueAt (
                            clip.highAutomation, t, 0.0f));
                    clip.filter.processSample (
                        left, right,
                        automationValueAt (
                            clip.filterAutomation, t, 0.0f));
                }

                float g = clip.gain;
                g *= automationGainAt (clip.gainAutomation, t);
                if (clip.fadeInSamples > 0 && since < clip.fadeInSamples)
                    g *= (float) since / (float) clip.fadeInSamples;
                if (clip.fadeOutSamples > 0 && since > clip.timelineLengthSamples - clip.fadeOutSamples)
                    g *= juce::jlimit (0.0f, 1.0f, (float) (clip.timelineLengthSamples - since) / (float) clip.fadeOutSamples);

                left *= g;
                right *= g;
                if (! clip.reverbAutomation.empty()
                    || ! clip.echoAutomation.empty())
                    if (clip.deckFx != nullptr)
                        clip.deckFx->processSample (
                        left, right,
                        automationValueAt (
                            clip.reverbAutomation, t, 0.0f),
                        automationValueAt (
                            clip.echoAutomation, t, 0.0f));

                const int i = (int) (t - pos);
                if (outCh == 1)
                    out.addSample (0, i, 0.5f * (left + right));
                else
                {
                    out.addSample (0, i, left);
                    out.addSample (1, i, right);
                    for (int ch = 2; ch < outCh; ++ch)
                        out.addSample (
                            ch, i, 0.5f * (left + right));
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
    static float automationGainAt (
        const std::vector<std::pair<juce::int64, float>>& points,
        juce::int64 timelineSample) noexcept
    {
        if (points.empty()) return 1.0f;
        if (timelineSample <= points.front().first) return points.front().second;
        if (timelineSample >= points.back().first)  return points.back().second;
        const auto next = std::lower_bound (
            points.begin(), points.end(), timelineSample,
            [] (const auto& point, juce::int64 sample)
            {
                return point.first < sample;
            });
        const auto& b = *next;
        const auto& a = *(next - 1);
        const auto span = juce::jmax<juce::int64> (1, b.first - a.first);
        const float alpha = (float) (timelineSample - a.first)
                            / (float) span;
        return a.second + alpha * (b.second - a.second);
    }

    static float automationValueAt (
        const std::vector<std::pair<juce::int64, float>>& points,
        juce::int64 timelineSample, float fallback) noexcept
    {
        if (points.empty()) return fallback;
        if (timelineSample <= points.front().first) return points.front().second;
        if (timelineSample >= points.back().first) return points.back().second;
        const auto next = std::lower_bound (
            points.begin(), points.end(), timelineSample,
            [] (const auto& point, juce::int64 sample)
            {
                return point.first < sample;
            });
        const auto& b = *next;
        const auto& a = *(next - 1);
        const auto span = juce::jmax<juce::int64> (1, b.first - a.first);
        const float alpha = (float) (timelineSample - a.first)
                            / (float) span;
        return a.second + alpha * (b.second - a.second);
    }

    static float automationStepAt (
        const std::vector<std::pair<juce::int64, float>>& points,
        juce::int64 timelineSample, float fallback) noexcept
    {
        if (points.empty()) return fallback;
        const auto next = std::upper_bound (
            points.begin(), points.end(), timelineSample,
            [] (juce::int64 sample, const auto& point)
            {
                return sample < point.first;
            });
        return next == points.begin() ? points.front().second
                                      : (next - 1)->second;
    }

    static MasterState masterStateAt (const MasterAutomation& automation,
                                      juce::int64 timelineSample) noexcept
    {
        MasterState state;
        const auto continuous = [&] (const auto& points, float& destination,
                                     MasterState::Presence presence)
        {
            if (points.empty()) return;
            destination = automationValueAt (points, timelineSample, destination);
            state.presence |= (uint32_t) presence;
        };
        const auto stepped = [&] (const auto& points, float& destination,
                                  MasterState::Presence presence)
        {
            if (points.empty()) return;
            destination = automationStepAt (points, timelineSample, destination);
            state.presence |= (uint32_t) presence;
        };

        continuous (automation.gainDb, state.gainDb, MasterState::gainPresent);
        continuous (automation.fxX, state.fxX, MasterState::fxXPresent);
        continuous (automation.fxY, state.fxY, MasterState::fxYPresent);
        continuous (automation.smartAmount, state.smartAmount,
                    MasterState::smartAmountPresent);
        stepped (automation.smartMode, state.smartMode,
                 MasterState::smartModePresent);
        stepped (automation.smartOn, state.smartOn,
                 MasterState::smartOnPresent);
        continuous (automation.limiterCeilingDb, state.limiterCeilingDb,
                    MasterState::limiterPresent);
        return state;
    }

    void resetClipDsp()
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (active == nullptr) return;
        for (const auto& clip : *active)
        {
            clip.eq.prepare (sr, 2);
            clip.filter.prepare (sr);
            if (clip.deckFx == nullptr)
                clip.deckFx = std::make_unique<DeckFX>();
            clip.deckFx->prepare (sr, 1);
            clip.eq.reset();
            clip.filter.reset();
            clip.deckFx->reset();
        }
    }

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
    std::shared_ptr<const MasterAutomation> masterAutomation;
    MasterState currentMasterState;
    std::atomic<bool>        playing { false };
    std::atomic<juce::int64> playhead { 0 };
    std::atomic<juce::int64> lengthSamples { 0 };
    std::atomic<bool>        loopEnabled { false };
    std::atomic<juce::int64> loopStartSamples { 0 };
    std::atomic<juce::int64> loopEndSamples { 0 };
};
