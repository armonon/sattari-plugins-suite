#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>

namespace stemdeck
{
// One performance-bus looper whose capture start is bar-quantized. It lives
// before master processing, so loop playback follows the same inserts/limiter as
// the sources and is printed into the master/retrospective safety lanes.
class LiveLooper
{
public:
    enum class State { empty, armed, recording, playing, overdubbing, stopped };

    void prepare (double newSampleRate, int maxBlockSize, int channels)
    {
        sampleRate = juce::jmax (8000.0, newSampleRate);
        preparedChannels = juce::jlimit (1, 2, channels);
        // Eight bars at the slowest supported tempo (40 BPM) = 48 seconds.
        const int capacity = juce::jmax (maxBlockSize,
            (int) std::ceil (sampleRate * 48.0));
        loop.setSize (preparedChannels, capacity, false, true, true);
        loop.clear();
        clearInternal();
    }

    void requestCapture (int bars) noexcept
    {
        requestedBars.store (juce::jlimit (1, 8, bars), std::memory_order_relaxed);
        pending.store ((int) Command::capture, std::memory_order_release);
    }
    void requestTogglePlayback() noexcept
    { pending.store ((int) Command::togglePlayback, std::memory_order_release); }
    void requestToggleOverdub() noexcept
    { pending.store ((int) Command::toggleOverdub, std::memory_order_release); }
    void requestClear() noexcept
    { pending.store ((int) Command::clear, std::memory_order_release); }

    State getState() const noexcept
    { return (State) state.load (std::memory_order_acquire); }
    bool hasLoop() const noexcept { return loopLength.load (std::memory_order_relaxed) > 0; }
    float progress() const noexcept
    {
        const int length = loopLength.load (std::memory_order_relaxed);
        return length > 0 ? (float) playhead.load (std::memory_order_relaxed) / (float) length : 0.0f;
    }
    int bars() const noexcept { return activeBars.load (std::memory_order_relaxed); }

    // AUDIO THREAD. The buffer initially contains the dry performance bus.
    void process (juce::AudioBuffer<float>& audio, juce::int64 timelineSample,
                  double bpm) noexcept
    {
        applyCommand (timelineSample, bpm);
        const int samples = audio.getNumSamples();
        const int channels = juce::jmin (preparedChannels, audio.getNumChannels());
        if (samples <= 0 || channels <= 0 || loop.getNumSamples() <= 0) return;

        auto current = getState();
        int begin = 0;
        if (current == State::armed)
        {
            const auto start = scheduledStart.load (std::memory_order_relaxed);
            if (timelineSample + samples <= start) return;
            begin = (int) juce::jlimit<juce::int64> (0, samples, start - timelineSample);
            state.store ((int) State::recording, std::memory_order_release);
            current = State::recording;
        }

        if (current == State::recording)
        {
            int write = playhead.load (std::memory_order_relaxed);
            const int target = targetLength.load (std::memory_order_relaxed);
            const int writable = juce::jmin (samples - begin, target - write);
            if (writable > 0)
                for (int ch = 0; ch < channels; ++ch)
                    loop.copyFrom (ch, write, audio, ch, begin, writable);
            write += juce::jmax (0, writable);
            playhead.store (write, std::memory_order_relaxed);
            if (write >= target)
            {
                loopLength.store (target, std::memory_order_release);
                playhead.store (0, std::memory_order_relaxed);
                state.store ((int) State::playing, std::memory_order_release);
                current = State::playing;
                begin += writable;
            }
            else return;
        }

        if (current != State::playing && current != State::overdubbing) return;
        const int length = loopLength.load (std::memory_order_relaxed);
        if (length <= 0) return;
        int read = playhead.load (std::memory_order_relaxed);
        for (int i = begin; i < samples; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const float existing = loop.getSample (ch, read);
                const float dry = audio.getSample (ch, i);
                if (current == State::overdubbing)
                    loop.setSample (ch, read, juce::jlimit (-1.0f, 1.0f,
                                                            existing + dry * 0.78f));
                audio.setSample (ch, i, dry + existing);
            }
            if (++read >= length) read = 0;
        }
        playhead.store (read, std::memory_order_relaxed);
    }

private:
    enum class Command { none, capture, togglePlayback, toggleOverdub, clear };

    void applyCommand (juce::int64 timelineSample, double bpm) noexcept
    {
        const auto command = (Command) pending.exchange ((int) Command::none,
                                                          std::memory_order_acq_rel);
        if (command == Command::none) return;
        // Clearing metadata is sufficient: the old samples are unreachable once
        // loopLength becomes zero, and the next capture overwrites its complete
        // active range. Zeroing a 48-second buffer here would stall the callback.
        if (command == Command::clear) { clearInternal(); return; }

        const auto current = getState();
        if (command == Command::capture)
        {
            const double safeBpm = juce::jlimit (40.0, 240.0, bpm);
            const int barSamples = juce::jmax (1, (int) std::llround (
                sampleRate * 60.0 * 4.0 / safeBpm));
            const int barsToUse = requestedBars.load (std::memory_order_relaxed);
            activeBars.store (barsToUse, std::memory_order_relaxed);
            targetLength.store (juce::jmin (loop.getNumSamples(), barSamples * barsToUse),
                                std::memory_order_relaxed);
            const auto nextBar = ((timelineSample + barSamples - 1) / barSamples) * barSamples;
            scheduledStart.store (nextBar, std::memory_order_relaxed);
            playhead.store (0, std::memory_order_relaxed);
            loopLength.store (0, std::memory_order_relaxed);
            state.store ((int) State::armed, std::memory_order_release);
        }
        else if (command == Command::togglePlayback && hasLoop())
        {
            state.store ((int) ((current == State::playing || current == State::overdubbing)
                                 ? State::stopped : State::playing),
                         std::memory_order_release);
        }
        else if (command == Command::toggleOverdub && hasLoop())
        {
            state.store ((int) (current == State::overdubbing ? State::playing
                                                               : State::overdubbing),
                         std::memory_order_release);
        }
    }

    void clearInternal() noexcept
    {
        playhead.store (0, std::memory_order_relaxed);
        loopLength.store (0, std::memory_order_relaxed);
        targetLength.store (0, std::memory_order_relaxed);
        scheduledStart.store (0, std::memory_order_relaxed);
        activeBars.store (4, std::memory_order_relaxed);
        state.store ((int) State::empty, std::memory_order_release);
        pending.store ((int) Command::none, std::memory_order_relaxed);
    }

    juce::AudioBuffer<float> loop;
    double sampleRate = 44100.0;
    int preparedChannels = 2;
    std::atomic<int> state { (int) State::empty };
    std::atomic<int> pending { (int) Command::none };
    std::atomic<int> requestedBars { 4 }, activeBars { 4 };
    std::atomic<int> playhead { 0 }, loopLength { 0 }, targetLength { 0 };
    std::atomic<juce::int64> scheduledStart { 0 };
};
} // namespace stemdeck
