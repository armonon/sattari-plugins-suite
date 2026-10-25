#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>

// Allocation coverage here is specific to mono scratch. It does not assert
// that third-party or GUI code is globally allocation-free.
struct TunerScratchContractAccess
{
    struct Snapshot { const float* data; size_t size; size_t capacity; };
    static Snapshot snapshot (const SattariTunerAudioProcessor& processor)
    {
        return { processor.monoScratch.data(), processor.monoScratch.size(),
                 processor.monoScratch.capacity() };
    }
    static bool unchanged (const Snapshot& before, const SattariTunerAudioProcessor& processor)
    {
        const auto after = snapshot (processor);
        return before.data == after.data && before.size == after.size
            && before.capacity == after.capacity;
    }
};

inline int runTunerOversizedRegression()
{
    // Genuine 2cc39b7 already detects the late tone but grows monoScratch.
    // The scratchStable assertion distinguishes this allocation-removal port
    // from the restored-source truncation fix with otherwise similar tests.
    int failures = 0;
    for (double rate : { 48000.0, 96000.0 })
        for (int channels : { 1, 2 })
        {
            SattariTunerAudioProcessor processor;
            auto layout = processor.getBusesLayout();
            layout.inputBuses.getReference (0) = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
            layout.outputBuses.getReference (0) = layout.inputBuses[0];
            const bool layoutOk = processor.setBusesLayout (layout);
            processor.prepareToPlay (rate, 512);
            const auto scratchBefore = TunerScratchContractAccess::snapshot (processor);
            const int samples = rate == 48000.0 ? 16384 : 32768;
            const int start = samples / 2;
            juce::AudioBuffer<float> audio (channels, samples);
            audio.clear();
            for (int i = start; i < samples; ++i)
            {
                const float value = 0.4f * (float) std::sin (
                    juce::MathConstants<double>::twoPi * 440.0 * (double) (i - start) / rate);
                for (int channel = 0; channel < channels; ++channel) audio.setSample (channel, i, value);
            }
            juce::AudioBuffer<float> original;
            original.makeCopyOf (audio);
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
            const auto reading = processor.getReading();
            const bool scratchStable = TunerScratchContractAccess::unchanged (scratchBefore, processor);
            bool exact = true;
            for (int channel = 0; channel < channels; ++channel)
                exact = exact && std::memcmp (audio.getReadPointer (channel), original.getReadPointer (channel),
                                               (size_t) samples * sizeof (float)) == 0;
            const bool ok = layoutOk && exact && scratchStable && reading.voiced && reading.midiNote == 69
                         && std::isfinite (reading.frequencyHz) && std::abs (reading.frequencyHz - 440.0f) <= 2.0f
                         && std::isfinite (reading.cents) && std::abs (reading.cents) <= 8.0f;
            std::printf ("Tuner late-tone rate=%.0f channels=%d samples=%d voiced=%d note=%d hz=%.4f cents=%.4f exact=%d scratchStable=%d result=%s\n",
                          rate, channels, samples, reading.voiced, reading.midiNote, reading.frequencyHz,
                          reading.cents, exact, scratchStable, ok ? "PASS" : "FAIL");
            if (! ok) ++failures;
            processor.releaseResources();
        }
    return failures;
}
