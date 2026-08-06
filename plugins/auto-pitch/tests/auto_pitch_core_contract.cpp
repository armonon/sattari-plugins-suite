#include "PluginProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace
{
class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setTimeInSeconds (seconds);
        info.setTimeInSamples ((juce::int64) std::round (seconds * 48000.0));
        info.setIsPlaying (true);
        return info;
    }
    double seconds = 0.0;
};

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

bool clickButtonNamed (juce::Component& parent, const juce::String& name)
{
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
    {
        auto* child = parent.getChildComponent (i);
        if (auto* button = dynamic_cast<juce::TextButton*> (child);
            button != nullptr && button->getButtonText().equalsIgnoreCase (name))
        {
            button->setToggleState (true, juce::dontSendNotification);
            if (button->onClick)
                button->onClick();
            return true;
        }
        if (child != nullptr && clickButtonNamed (*child, name))
            return true;
    }
    return false;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3
        && (std::strcmp (argv[1], "--render-ui") == 0
            || std::strcmp (argv[1], "--render-ui-min") == 0
            || std::strcmp (argv[1], "--render-ui-pro") == 0
            || std::strcmp (argv[1], "--render-ui-edit") == 0))
    {
        SattariAutoPitchAudioProcessor processor;
        const bool minimumSize = std::strcmp (argv[1], "--render-ui-min") == 0;
        const bool proTab = std::strcmp (argv[1], "--render-ui-pro") == 0;
        const bool editTab = std::strcmp (argv[1], "--render-ui-edit") == 0;
        const juce::File destination (argv[2]);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        bool ok = editor != nullptr;
        if (ok)
        {
            editor->setSize (minimumSize ? 960 : 1120, minimumSize ? 680 : 700);
            if (proTab)
                ok = clickButtonNamed (*editor, "PRO");
            if (editTab)
                ok = clickButtonNamed (*editor, "EDIT");
            if (ok)
                ok = sattari::test::renderComponentToPng (*editor, destination);
        }
        std::printf ("%s %s\n", ok ? "rendered" : "FAIL render",
                     destination.getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }

    SattariAutoPitchAudioProcessor processor;
    // Hosts are allowed to renegotiate format repeatedly. This also exercises
    // safe teardown/reload of the embedded ONNX session and its environment.
    processor.prepareToPlay (44100.0, 256);
    processor.prepareToPlay (48000.0, 512);

   #if SATTARI_HAS_ONNX
    if (! processor.neuralModelPresent())
    {
        std::cerr << "ONNX build did not load the embedded CREPE model\n";
        return 1;
    }
   #endif

    auto& apvts = processor.parameters();
    const juce::StringArray parityParameters {
        "scaleMode", "referenceDetune", "followReference", "midiTarget",
        "capturePitchEdits", "usePitchEdits", "graphTimeCorrection",
        "addVibrato", "vibratoRate", "vibratoOnset"
    };
    for (const auto& id : parityParameters)
        if (apvts.getParameter (id) == nullptr)
        {
            std::cerr << "missing parity parameter: " << id << "\n";
            return 1;
        }

    setParameter (apvts, "followReference", 0.0f);
    setParameter (apvts, "referenceDetune", 100.0f);
    if (std::abs (processor.currentReferenceHz() - 466.1637615) > 0.01)
    {
        std::cerr << "reference detune did not move A4 by +100 cents: "
                  << processor.currentReferenceHz() << " Hz\n";
        return 1;
    }
    setParameter (apvts, "referenceDetune", 0.0f);

    // Graph objects are stable-ID entities with reversible structural and
    // per-object edits. Capture through the real detector/transport path so this
    // contract covers the hand-off from audio analysis to the editor model.
    {
        TestPlayHead playHead;
        processor.setPlayHead (&playHead);
        setParameter (apvts, "capturePitchEdits", 1.0f);
        double capturePhase = 0.0;
        for (int block = 0; block < 24; ++block)
        {
            juce::AudioBuffer<float> captureBuffer (2, 512);
            for (int i = 0; i < captureBuffer.getNumSamples(); ++i)
            {
                const float sample = 0.2f * std::sin ((float) capturePhase);
                capturePhase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
                captureBuffer.setSample (0, i, sample);
                captureBuffer.setSample (1, i, sample);
            }
            juce::MidiBuffer midi;
            processor.processBlock (captureBuffer, midi);
            playHead.seconds += 512.0 / 48000.0;
        }
        setParameter (apvts, "capturePitchEdits", 0.0f);
        processor.setPlayHead (nullptr);

        auto edits = processor.getPitchEdits();
        if (edits.empty() || edits.front().id == 0)
        {
            std::cerr << "graph capture did not produce a stable note object\n";
            return 1;
        }
        const auto original = edits.front();
        if (! processor.splitPitchEdit (original.id, 0.5 * (original.startSec + original.endSec))
            || processor.getPitchEdits().size() != edits.size() + 1)
        {
            std::cerr << "graph split failed\n";
            return 1;
        }
        auto changed = processor.getPitchEdits().front();
        changed.amount = 0.37f;
        changed.retune = 0.91f;
        changed.formantSemitones = 3.0f;
        changed.timeOffsetSec = -0.125;
        changed.gainDb = -2.5f;
        if (! processor.setPitchEditEntry (changed.id, changed)
            || std::abs (processor.getPitchEdits().front().timeOffsetSec + 0.125) > 1.0e-6)
        {
            std::cerr << "per-object graph controls failed\n";
            return 1;
        }
        juce::MemoryBlock graphState;
        processor.getStateInformation (graphState);
        SattariAutoPitchAudioProcessor restoredGraph;
        restoredGraph.setStateInformation (graphState.getData(), (int) graphState.getSize());
        const auto restoredEdits = restoredGraph.getPitchEdits();
        if (restoredEdits.empty() || restoredEdits.front().id != changed.id
            || std::abs (restoredEdits.front().amount - 0.37f) > 0.001f
            || std::abs (restoredEdits.front().formantSemitones - 3.0f) > 0.001f
            || std::abs (restoredEdits.front().timeOffsetSec + 0.125) > 1.0e-6
            || std::abs (restoredEdits.front().gainDb + 2.5f) > 0.001f)
        {
            std::cerr << "graph object state round-trip failed\n";
            return 1;
        }
        if (! processor.undoPitchEdit() || ! processor.redoPitchEdit())
        {
            std::cerr << "graph undo/redo failed\n";
            return 1;
        }
        if (! processor.joinPitchEdit (changed.id) || ! processor.deletePitchEdit (changed.id))
        {
            std::cerr << "graph join/delete failed\n";
            return 1;
        }
        processor.clearPitchEdits();
        std::cout << "  graphObjectEditing=PASS (split/join/delete, object controls, undo/redo)\n";
    }

    {
        SattariAutoPitchAudioProcessor baselineLatency;
        baselineLatency.prepareToPlay (48000.0, 512);
        SattariAutoPitchAudioProcessor timedLatency;
        setParameter (timedLatency.parameters(), "graphTimeCorrection", 1.0f);
        timedLatency.prepareToPlay (48000.0, 512);
        const int added = timedLatency.getLatencySamples() - baselineLatency.getLatencySamples();
        if (added != 24000)
        {
            std::cerr << "graph time correction did not report its 500 ms look-ahead: " << added << " samples\n";
            return 1;
        }
        std::cout << "  graphTimeLatency=PASS (24000 samples reported at 48 kHz)\n";
    }

    setParameter (apvts, "keyRoot", 0.0f); // C
    setParameter (apvts, "scale", 0.0f); // major
    setParameter (apvts, "scaleMode", 0.0f); // major
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
        setParameter (nApvts, "scaleMode", 0.0f); // major
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
        setParameter (modeApvts, "scaleMode", 0.0f);
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

    // Extended scales are real correction modes, not labels grafted onto the
    // legacy three-choice parameter. The summary reports the effective mode.
    setParameter (apvts, "scaleMode", 3.0f); // Dorian
    if (! processor.getKeyDetectionTabSummary().containsIgnoreCase ("dorian"))
    {
        std::cerr << "extended scale mode did not become effective: "
                  << processor.getKeyDetectionTabSummary() << "\n";
        return 1;
    }

    // Held MIDI must become an exact note target (independent of MIDI Learn).
    {
        SattariAutoPitchAudioProcessor midiProcessor;
        midiProcessor.prepareToPlay (sampleRate, blockSize);
        auto& m = midiProcessor.parameters();
        setParameter (m, "keyRoot", 0.0f);
        setParameter (m, "scale", 0.0f);
        setParameter (m, "scaleMode", 0.0f);
        setParameter (m, "midiTarget", 1.0f);
        setParameter (m, "amount", 1.0f);
        setParameter (m, "speed", 1.0f);
        setParameter (m, "doNoHarm", 0.0f);

        double midiPhase = 0.0;
        for (int block = 0; block < 60; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const float sample = 0.18f * std::sin ((float) midiPhase);
                midiPhase += juce::MathConstants<double>::twoPi * 440.0 / sampleRate;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0); // C5
            midiProcessor.processBlock (buffer, midi);
        }
        const auto midiStatus = midiProcessor.getLiveStatusSummary();
        if (! midiStatus.contains (juce::String::fromUTF8 ("A4 \xe2\x86\x92 C5")))
        {
            std::cerr << "MIDI note target was not applied: " << midiStatus << "\n";
            return 1;
        }
    }

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
