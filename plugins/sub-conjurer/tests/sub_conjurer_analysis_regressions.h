#pragma once

#include "SubConjurerAnalysisFeed.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <vector>

// Friend-only inspection proves that the callback does not grow/reallocate the
// prepared scratch. This fails on the genuine-history baseline for large blocks.
struct SubConjurerScratchProbe
{
    struct Snapshot { const float* data; std::size_t size; std::size_t capacity; };
    static Snapshot capture (const SubConjurerAudioProcessor& processor)
    {
        return { processor.monoScratch.data(), processor.monoScratch.size(),
                 processor.monoScratch.capacity() };
    }
    static bool unchanged (const Snapshot& before, const SubConjurerAudioProcessor& processor)
    {
        const auto after = capture (processor);
        return before.data == after.data && before.size == after.size
            && before.capacity == after.capacity;
    }
};

// Invoked by the existing SubConjurerContract main. Tests the production helper
// and full-callback processor input preservation.
inline int runSubConjurerAnalysisRegressions()
{
    struct Sink
    {
        std::vector<float> samples;
        std::vector<int> chunks;
        void pushBlock (const float* values, int count)
        {
            samples.insert (samples.end(), values, values + count);
            chunks.push_back (count);
        }
    };
    int failures = 0;
    for (const int channels : { 0, 1, 2 })
        for (const int samples : { 0, 1, 511, 512, 513, 8191, 8192, 8193, 16384 })
            for (const int capacity : { 0, 1, 17, 8192 })
            {
                std::vector<float> left ((size_t) samples), right ((size_t) samples);
                std::vector<float> expected ((size_t) samples);
                for (int i = 0; i < samples; ++i)
                {
                    left[(size_t) i] = (float) (i % 97) / 128.0f;
                    right[(size_t) i] = -(float) (i % 53) / 64.0f;
                    // Independent per-sample oracle: no production chunk loop.
                    expected[(size_t) i] = channels == 0 ? 0.0f : channels == 1
                        ? left[(size_t) i] : left[(size_t) i] / 2.0f + right[(size_t) i] / 2.0f;
                }
                const float* inputs[] = { left.data(), right.data() };
                constexpr float canary = 9876.0f;
                std::vector<float> scratch ((size_t) capacity + 2, canary);
                Sink sink;
                sink.samples.reserve ((size_t) samples * 2);
                sink.chunks.reserve ((size_t) samples * 2);
                // Repeated calls catch omitted tails and accidental replay/order loss.
                for (int repeat = 0; repeat < 2; ++repeat)
                    sattari::subconjurer::feedPitchInput (
                        channels == 0 ? nullptr : inputs, channels, samples,
                        capacity == 0 ? nullptr : scratch.data() + 1, capacity, sink);
                bool ok = sink.samples.size() == (size_t) samples * 2
                       && scratch.front() == canary && scratch.back() == canary;
                for (size_t i = 0; ok && i < sink.samples.size(); ++i)
                    ok = sink.samples[i] == expected[i % (size_t) samples];
                for (const int count : sink.chunks)
                    ok = ok && count > 0 && count <= std::max (1, capacity);
                if (! ok)
                {
                    ++failures;
                    std::fprintf (stderr, "FAIL analysis stream channels=%d samples=%d capacity=%d\n",
                                  channels, samples, capacity);
                }
            }
    // Null storage must use the stack fallback even if the caller supplied a
    // positive capacity; zero capacity with nonnull storage must not touch it.
    {
        const float values[] = { 0.25f, -0.5f, 0.75f };
        const float* input[] = { values };
        float untouched = 9876.0f;
        Sink nullSink, zeroSink;
        nullSink.samples.reserve (3); nullSink.chunks.reserve (3);
        zeroSink.samples.reserve (3); zeroSink.chunks.reserve (3);
        sattari::subconjurer::feedPitchInput (input, 1, 3, nullptr, 32, nullSink);
        sattari::subconjurer::feedPitchInput (input, 1, 3, &untouched, 0, zeroSink);
        if (nullSink.samples != std::vector<float> (values, values + 3)
            || zeroSink.samples != nullSink.samples || untouched != 9876.0f)
        {
            ++failures;
            std::fprintf (stderr, "FAIL analysis empty-scratch fallback\n");
        }
    }

    // Real processor preservation oracle: only the late half carries a tone.
    // The genuine-history baseline already consumes it, but grows scratch in the
    // callback. Keep this behavior while removing that growth.
    for (const double rate : { 48000.0, 96000.0 })
    {
        SubConjurerAudioProcessor processor;
        auto& params = processor.parameters();
        const auto set = [&params] (const char* id, float value)
        {
            if (auto* parameter = params.getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };
        set ("mode", 0.0f); set ("tuning", 0.0f); set ("glide", 0.0f);
        set ("amount", 1.0f); set ("subonly", 1.0f); set ("followKey", 0.0f);
        processor.prepareToPlay (rate, 512);
        const auto scratchBefore = SubConjurerScratchProbe::capture (processor);
        const int samples = rate < 96000.0 ? 16384 : 32768;
        const int start = samples / 2;
        juce::AudioBuffer<float> audio (2, samples);
        audio.clear();
        for (int i = start; i < samples; ++i)
        {
            const float value = 0.5f * (float) std::sin (
                juce::MathConstants<double>::twoPi * 75.0 * (double) (i - start) / rate);
            audio.setSample (0, i, value); audio.setSample (1, i, value);
        }
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        if (! SubConjurerScratchProbe::unchanged (scratchBefore, processor))
        {
            ++failures;
            std::fprintf (stderr, "FAIL callback changed prepared scratch storage\n");
        }
        const auto ui = processor.currentUiState();
        bool finiteBounded = true;
        double tailEnergy = 0.0;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < samples; ++i)
            {
                const float value = audio.getSample (channel, i);
                finiteBounded = finiteBounded && std::isfinite (value) && std::abs (value) <= 0.981f;
                if (i >= start) tailEnergy += (double) value * value;
            }
        if (! finiteBounded || tailEnergy <= 1.0e-4 || std::abs (ui.frequencyHz - 75.0f) > 2.0f)
        {
            ++failures;
            std::fprintf (stderr, "FAIL late-tone %.0fHz sample-rate: tracked %.3fHz energy %.6f\n",
                          rate, ui.frequencyHz, tailEnergy);
        }
        processor.releaseResources();
    }
    // Envelope and metering must use each original input frame, not the last
    // analysis chunk. Asymmetric stereo and an early peak distinguish stale
    // scratch reuse; dry output also detects reading channels after mutation.
    for (const int channels : { 1, 2 })
    {
        SubConjurerAudioProcessor processor;
        auto layout = processor.getBusesLayout();
        const auto channelSet = channels == 1 ? juce::AudioChannelSet::mono()
                                              : juce::AudioChannelSet::stereo();
        layout.inputBuses.set (0, channelSet);
        layout.outputBuses.set (0, channelSet);
        if (! processor.setBusesLayout (layout))
        {
            ++failures;
            std::fprintf (stderr, "FAIL envelope bus layout channels=%d\n", channels);
            continue;
        }
        auto& params = processor.parameters();
        const auto set = [&params] (const char* id, float value)
        {
            if (auto* parameter = params.getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };
        set ("amount", 0.0f); set ("dry", 0.5f); set ("subonly", 0.0f);
        set ("followKey", 0.0f);
        constexpr double rate = 48000.0;
        constexpr int samples = 1537;
        processor.prepareToPlay (rate, 32);
        const auto scratchBefore = SubConjurerScratchProbe::capture (processor);
        juce::AudioBuffer<float> audio (channels, samples), original (channels, samples);
        sattari::dsp::EnvelopeFollower reference;
        reference.prepare (rate);
        reference.setTimes (params.getRawParameterValue ("attack")->load() * 1000.0f,
                            params.getRawParameterValue ("release")->load() * 1000.0f);
        float expectedPeak = 0.0f;
        for (int i = 0; i < samples; ++i)
        {
            float mono = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
            {
                const float value = i == 0 ? 0.9f : channel == 0
                    ? (float) (i % 71) / 100.0f - 0.35f
                    : (float) (i % 43) / 100.0f - 0.21f;
                audio.setSample (channel, i, value);
                original.setSample (channel, i, value);
                mono += value / (float) channels;
            }
            expectedPeak = std::max (expectedPeak, std::abs (mono));
            reference.processRectified (mono);
        }
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        if (! SubConjurerScratchProbe::unchanged (scratchBefore, processor))
        {
            ++failures;
            std::fprintf (stderr, "FAIL callback changed prepared scratch storage\n");
        }
        const auto ui = processor.currentUiState();
        bool dryPreserved = true;
        for (int channel = 0; channel < channels; ++channel)
            for (int i = 0; i < samples; ++i)
                dryPreserved = dryPreserved
                    && audio.getSample (channel, i) == original.getSample (channel, i) * 0.5f;
        if (! dryPreserved || std::abs (ui.inputPeak - expectedPeak) > 1.0e-6f
                           || std::abs (ui.envelope - reference.current()) > 1.0e-6f)
        {
            ++failures;
            std::fprintf (stderr, "FAIL whole-input envelope/peak/dry channels=%d\n", channels);
        }
        processor.releaseResources();
    }

    std::printf ("Sub Conjurer analysis regressions: %d failures\n", failures);
    return failures;
}
