#include "PluginProcessor.h"

#include <cmath>
#include <iostream>

namespace
{
void setParameter (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    auto* parameter = apvts.getParameter (id);
    if (parameter == nullptr)
        throw std::runtime_error ("missing parameter: " + id.toStdString());

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

double rmsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double sum = 0.0;
    int count = 0;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
    {
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const double diff = (double) a.getSample (channel, i) - (double) b.getSample (channel, i);
            sum += diff * diff;
            ++count;
        }
    }

    return std::sqrt (sum / (double) juce::jmax (1, count));
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    SattariAutoPitchAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto& apvts = processor.parameters();
    setParameter (apvts, "keyRoot", 0.0f); // C
    setParameter (apvts, "scale", 0.0f); // major
    setParameter (apvts, "style", 2.0f); // hard
    setParameter (apvts, "amount", 1.0f);
    setParameter (apvts, "speed", 1.0f);
    setParameter (apvts, "enableCorrection", 1.0f);
    setParameter (apvts, "doNoHarm", 0.0f);

    constexpr int blockSize = 512;
    constexpr int blocks = 60;
    constexpr double sampleRate = 48000.0;
    const double detunedFrequency = 440.0 * std::pow (2.0, 35.0 / 1200.0); // A4 +35 cents

    double phase = 0.0;
    double changedEnergy = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float sample = 0.18f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * detunedFrequency / sampleRate;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, i, sample);
        }

        juce::AudioBuffer<float> dry;
        dry.makeCopyOf (buffer);
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        changedEnergy += rmsDifference (buffer, dry);
    }

    const auto status = processor.getLiveStatusSummary();
    // Assert on what proves the engine ran, not on marketing copy: the status
    // must name either the real Rubber Band engine or the fallback guide.
    if (! (status.containsIgnoreCase ("Rubber Band") || status.containsIgnoreCase ("fallback")))
    {
        std::cerr << "unexpected status: " << status << "\n";
        return 1;
    }

    // The Key-Detection readout was intentionally minimised (was a paragraph of
    // harmonic-wheel / Send-to-Tune jargon). It must now just state the key + confidence.
    const auto keyTabSummary = processor.getKeyDetectionTabSummary();
    if (! keyTabSummary.containsIgnoreCase ("Key")
        || ! keyTabSummary.containsIgnoreCase ("confidence"))
    {
        std::cerr << "key detection tab summary missing key/confidence: " << keyTabSummary << "\n";
        return 1;
    }

    if (changedEnergy <= 0.01)
    {
        std::cerr << "pitch path did not audibly alter the detuned test signal; changedEnergy=" << changedEnergy << "\n";
        return 1;
    }

    // --- YIN noise-robustness phase ------------------------------------------
    // Feed a 220 Hz (A3) fundamental buried under a strong 2nd harmonic (440 Hz)
    // AND broadband noise. Plain normalised autocorrelation tends to octave-jump
    // here (the 440 Hz partial + noise pull the peak to the wrong lag). YIN's
    // CMNDF + continuity prior should still report the A3 octave. We assert the
    // detected input note name contains "A3" - i.e. correct octave under noise.
    {
        SattariAutoPitchAudioProcessor noiseProc;
        noiseProc.prepareToPlay (48000.0, 512);
        auto& nApvts = noiseProc.parameters();
        setParameter (nApvts, "keyRoot", 9.0f);   // A
        setParameter (nApvts, "scale", 0.0f);     // major
        setParameter (nApvts, "enableCorrection", 1.0f);
        setParameter (nApvts, "doNoHarm", 0.0f);
        setParameter (nApvts, "vocalRange", 0.0f); // Wide

        const double f0 = 220.0;                    // A3
        double ph1 = 0.0, ph2 = 0.0;
        unsigned int rngState = 0x12345u;
        auto whiteNoise = [&rngState]() {
            rngState = rngState * 1664525u + 1013904223u;
            return ((double) rngState / (double) 0xFFFFFFFFu) * 2.0 - 1.0;
        };
        for (int block = 0; block < 80; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const double fund = 0.16 * std::sin (ph1);
                const double harm = 0.20 * std::sin (ph2);   // louder 2nd harmonic
                const double noise = 0.07 * whiteNoise();     // broadband contamination
                ph1 += juce::MathConstants<double>::twoPi * f0       / sampleRate;
                ph2 += juce::MathConstants<double>::twoPi * (2.0*f0) / sampleRate;
                const float s = (float) (fund + harm + noise);
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample (channel, i, s);
            }
            juce::MidiBuffer midi;
            noiseProc.processBlock (buffer, midi);
        }

        // Live status is now compact ("engine · key · A3 -> A3 · ..."); the detected
        // input note must still read A3 (correct octave under noise).
        const auto noiseStatus = noiseProc.getLiveStatusSummary();
        if (! noiseStatus.contains ("A3"))
        {
            std::cerr << "YIN failed noise-robustness: expected detected note A3, got: "
                      << noiseStatus << "\n";
            return 1;
        }
        std::cout << "  noiseRobustness=PASS (tracked A3 through 2nd-harmonic + broadband noise)\n";
    }

    const juce::StringArray expectedModes { "Natural", "Classic", "Modern Rap", "Robot", "Big Harmony" };
    for (int mode = 0; mode < expectedModes.size(); ++mode)
    {
        SattariAutoPitchAudioProcessor modeProcessor;
        modeProcessor.prepareToPlay (48000.0, 512);
        auto& modeApvts = modeProcessor.parameters();
        setParameter (modeApvts, "keyRoot", 0.0f);
        setParameter (modeApvts, "scale", 0.0f);
        setParameter (modeApvts, "style", (float) mode);
        setParameter (modeApvts, "amount", 1.0f);
        setParameter (modeApvts, "speed", 1.0f);
        setParameter (modeApvts, "enableCorrection", 1.0f);
        setParameter (modeApvts, "doNoHarm", 0.0f);

        double modePhase = 0.0;
        double modeChangedEnergy = 0.0;
        for (int block = 0; block < 30; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const float sample = 0.18f * std::sin ((float) modePhase);
                modePhase += juce::MathConstants<double>::twoPi * detunedFrequency / sampleRate;
                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample (channel, i, sample);
            }

            juce::AudioBuffer<float> dry;
            dry.makeCopyOf (buffer);
            juce::MidiBuffer midi;
            modeProcessor.processBlock (buffer, midi);
            modeChangedEnergy += rmsDifference (buffer, dry);
        }

        const auto modeStatus = modeProcessor.getLiveStatusSummary();
        if (! modeStatus.contains (expectedModes[mode]))   // compact status shows the Style name
        {
            std::cerr << "mode status missing " << expectedModes[mode] << ": " << modeStatus << "\n";
            return 1;
        }

        if (modeChangedEnergy <= 0.005)
        {
            std::cerr << "mode did not produce audible correction: " << expectedModes[mode]
                      << ", changedEnergy=" << modeChangedEnergy << "\n";
            return 1;
        }
    }

#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    // ---- Throat / Formant Shift must actually alter the signal -----------------
    //
    // These two controls were completely inert. Rubber Band's setFormantScale is
    // `if (m_r3) m_r3->setFormantScale(scale);` and Auto Pitch built the R2 engine, so
    // every call was silently discarded — in all three modes, and the same for Harmony.
    // Nothing here asserted on formants, so a knob documented in our own source as
    // "Antares' Throat Length analogue" shipped doing nothing at all.
    //
    // Engaging a formant control now selects R3, which changes the reported latency. Both
    // facts are checked: the latency move proves the engine really swapped, and the
    // spectral change proves the swap reached the audio.
    {
        auto renderTone = [&processor] (float throatValue, float formantSemis,
                                        juce::AudioBuffer<float>& out)
        {
            setParameter (processor.parameters(), "throat", throatValue);
            setParameter (processor.parameters(), "formantShift", formantSemis);
            // The engine swap is built on the message thread and exchanged by the audio
            // thread, so pump the message loop then run enough blocks for it to land and
            // for the new pipeline to fill past its start delay.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);

            const int blockSize = 512;
            juce::AudioBuffer<float> block (2, blockSize);
            out.setSize (2, blockSize * 24);
            out.clear();
            juce::MidiBuffer midi;
            double phase = 0.0;
            const double inc = 2.0 * juce::MathConstants<double>::pi * 160.0 / 48000.0;
            for (int b = 0; b < 24; ++b)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    // A tone with strong harmonics: formant scaling is only visible on a
                    // spectrum that has something above the fundamental to move.
                    const float v = (float) (0.5 * std::sin (phase)
                                           + 0.3 * std::sin (2.0 * phase)
                                           + 0.2 * std::sin (3.0 * phase)
                                           + 0.1 * std::sin (5.0 * phase));
                    phase += inc;
                    block.setSample (0, i, v);
                    block.setSample (1, i, v);
                }
                processor.processBlock (block, midi);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, b * blockSize, block, ch, 0, blockSize);
            }
            // The swap itself happens inside processBlock and re-triggers the async
            // update, so the new latency is only published after another turn of the
            // message loop. Pump again before the caller reads getLatencySamples().
            juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
        };

        // Zero-crossing rate is a cheap, robust proxy for spectral centroid: raising the
        // formants moves energy upward and the rate rises. Avoids pulling in an FFT.
        auto crossingRate = [] (const juce::AudioBuffer<float>& b)
        {
            int crossings = 0, counted = 0;
            const float* d = b.getReadPointer (0);
            // Skip the first half: it contains the pipeline start-delay transient.
            for (int i = b.getNumSamples() / 2 + 1; i < b.getNumSamples(); ++i)
            {
                if ((d[i - 1] <= 0.0f) != (d[i] <= 0.0f)) ++crossings;
                ++counted;
            }
            return counted > 0 ? (double) crossings / (double) counted : 0.0;
        };

        juce::AudioBuffer<float> neutral, shifted;
        renderTone (1.0f, 0.0f, neutral);
        const int neutralLatency = processor.getLatencySamples();

        renderTone (1.0f, 7.0f, shifted);      // +7 semitones of formant shift
        const int shiftedLatency = processor.getLatencySamples();

        if (shiftedLatency <= neutralLatency)
        {
            std::cerr << "formant engagement did not switch engine: latency "
                      << neutralLatency << " -> " << shiftedLatency
                      << " (expected an increase; R3 costs about double R2)\n";
            return 1;
        }

        const double neutralRate = crossingRate (neutral);
        const double shiftedRate = crossingRate (shifted);
        if (! (shiftedRate > neutralRate * 1.02))
        {
            std::cerr << "Formant Shift did not change the spectrum: zero-crossing rate "
                      << neutralRate << " -> " << shiftedRate
                      << " (a dead control leaves this identical)\n";
            return 1;
        }

        std::cout << "  formant: latency " << neutralLatency << " -> " << shiftedLatency
                  << " samples, zcr " << neutralRate << " -> " << shiftedRate << "\n";

        setParameter (processor.parameters(), "throat", 1.0f);
        setParameter (processor.parameters(), "formantShift", 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    }
#endif

    std::cout << "Auto Pitch core contract ok\n";
    std::cout << "  status=" << status << "\n";
    std::cout << "  keyTab=" << keyTabSummary << "\n";
    std::cout << "  changedEnergy=" << changedEnergy << "\n";
    std::cout << "  modes=Natural, Classic, Modern Rap, Robot, Big Harmony\n";
#if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    std::cout << "  engine=rubberband\n";
#else
    std::cout << "  engine=fallback-guide\n";
#endif
    return 0;
}
