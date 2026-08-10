#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <cstdio>

namespace sattari::test
{

inline void setRealParameter (juce::AudioProcessorValueTreeState& state,
                              const juce::String& id,
                              float value)
{
    if (auto* parameter = state.getParameter (id))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

template <typename Processor>
int runRackProductContract (const char* expectedName,
                            int expectedModules,
                            int minimumPrograms)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    Processor processor;
    int failures = 0;

    const auto fail = [&] (const char* message)
    {
        std::printf ("  FAIL %s\n", message);
        ++failures;
    };

    if (processor.getName() != expectedName) fail ("product identity mismatch");
    if (processor.numModules() != expectedModules) fail ("module roster count mismatch");
    if (processor.getNumPrograms() < minimumPrograms) fail ("factory workflow count too small");

    const auto menu = processor.moduleMenuNames();
    if (menu.size() != expectedModules + 1 || menu[0] != "Empty")
        fail ("module menu contract mismatch");

    auto& state = processor.parameters();
    const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
    const int blockSizes[] = { 64, 512 };

    for (double sampleRate : sampleRates)
    {
        for (int blockSize : blockSizes)
        {
            processor.prepareToPlay (sampleRate, blockSize);

            for (int program = 0; program < processor.getNumPrograms(); ++program)
            {
                processor.setCurrentProgram (program);
                processor.reset();

                if (state.getRawParameterValue (processor.slotParamId (0))->load() <= 0.0f)
                    fail ("factory workflow has an empty first slot");

                bool changedAudio = false;
                double lowPhase = 0.0;
                double highPhase = 0.0;
                const int passes = juce::jmax (12,
                    (int) std::ceil (sampleRate * 0.20 / (double) blockSize));
                for (int pass = 0; pass < passes; ++pass)
                {
                    juce::AudioBuffer<float> buffer (2, blockSize);
                    juce::AudioBuffer<float> original (2, blockSize);
                    for (int sample = 0; sample < blockSize; ++sample)
                    {
                        const float input = 0.15f * std::sin ((float) lowPhase)
                            + 0.10f * std::sin ((float) highPhase)
                            + (pass == 0 && sample == 0 ? 0.25f : 0.0f);
                        lowPhase += juce::MathConstants<double>::twoPi * 173.0 / sampleRate;
                        highPhase += juce::MathConstants<double>::twoPi * 6350.0 / sampleRate;
                        for (int channel = 0; channel < 2; ++channel)
                        {
                            buffer.setSample (channel, sample, input);
                            original.setSample (channel, sample, input);
                        }
                    }

                    juce::MidiBuffer midi;
                    processor.processBlock (buffer, midi);
                    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                        {
                            const float output = buffer.getSample (channel, sample);
                            if (! std::isfinite (output)) fail ("non-finite output");
                            if (std::abs (output) > 8.0f) fail ("unbounded output");
                            changedAudio = changedAudio
                                || std::abs (output - original.getSample (channel, sample)) > 1.0e-5f;
                        }
                }

                if (! changedAudio)
                {
                    std::printf ("  FAIL unchanged workflow: %s @ %.0f Hz / %d samples\n",
                                 processor.getProgramName (program).toRawUTF8(),
                                 sampleRate, blockSize);
                    ++failures;
                }
            }
        }
    }

    // Host state must restore the complete rack wiring, not just module values.
    processor.setCurrentProgram (0);
    const float savedSlot = state.getRawParameterValue (processor.slotParamId (0))->load();
    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    setRealParameter (state, processor.slotParamId (0), 0.0f);
    processor.setStateInformation (savedState.getData(), (int) savedState.getSize());
    if (std::abs (state.getRawParameterValue (processor.slotParamId (0))->load() - savedSlot) > 1.0e-6f)
        fail ("rack wiring did not restore from host state");

    // An empty rack is required to be a transparent wire.
    for (int slot = 0; slot < processor.kNumSlots; ++slot)
        setRealParameter (state, processor.slotParamId (slot), 0.0f);
    processor.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> bypass (2, 256);
    for (int channel = 0; channel < bypass.getNumChannels(); ++channel)
        for (int sample = 0; sample < bypass.getNumSamples(); ++sample)
            bypass.setSample (channel, sample,
                0.2f * std::sin (0.031f * (float) sample + 0.4f * (float) channel));
    juce::AudioBuffer<float> expected;
    expected.makeCopyOf (bypass);
    juce::MidiBuffer midi;
    processor.processBlock (bypass, midi);
    float bypassDifference = 0.0f;
    for (int channel = 0; channel < bypass.getNumChannels(); ++channel)
        for (int sample = 0; sample < bypass.getNumSamples(); ++sample)
            bypassDifference = juce::jmax (bypassDifference,
                std::abs (bypass.getSample (channel, sample)
                          - expected.getSample (channel, sample)));
    if (bypassDifference > 0.0f) fail ("empty rack is not bit-transparent");

    std::printf (failures == 0
        ? "%s product contract ok: modules=%d programs=%d rates=3 blocks=2\n"
        : "%s product contract FAILED: failures=%d\n",
        expectedName,
        failures == 0 ? expectedModules : failures,
        failures == 0 ? processor.getNumPrograms() : 0);
    return failures == 0 ? 0 : 1;
}

} // namespace sattari::test
