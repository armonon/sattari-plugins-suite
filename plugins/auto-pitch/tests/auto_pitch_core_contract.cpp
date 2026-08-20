#include "PluginProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>
#include <sattari_audio_core/SattariRenderHarness.h>

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
            || std::strcmp (argv[1], "--render-ui-key") == 0
            || std::strcmp (argv[1], "--render-ui-edit") == 0))
    {
        SattariAutoPitchAudioProcessor processor;
        const bool minimumSize = std::strcmp (argv[1], "--render-ui-min") == 0;
        //  The tab strip is gone — one page now. "--render-ui-pro" renders with the
        //  ADVANCED drawer open (the old PRO content), and "--render-ui-edit" is the
        //  same page as the default since the graph is always visible.
        const bool proTab = std::strcmp (argv[1], "--render-ui-pro") == 0;
        const bool keyWs  = std::strcmp (argv[1], "--render-ui-key") == 0;
        const bool editWs = std::strcmp (argv[1], "--render-ui-edit") == 0;
        const juce::File destination (argv[2]);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        bool ok = editor != nullptr;
        if (ok)
        {
            //  Render at sizes a host can actually produce: the editor's own limits
            //  are 960x760 - 1600x1100, and the old 700-high render silently
            //  clipped the very drawer the pro screenshot exists to prove.
            editor->setSize (minimumSize ? 960 : 1120, minimumSize ? 760 : 820);
            if (proTab)
                ok = clickButtonNamed (*editor, "ADVANCED");
            if (keyWs)
                ok = clickButtonNamed (*editor, "KEY");
            if (editWs)
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

    // Factory workflows are complete states, not sticky deltas. Reproduce the
    // real UI sequence that previously failed: load MIDI Directed, then Natural.
    // The second choice must clear MIDI/graph/transpose/poly/vibrato modes and
    // restore its own stronger natural-correction calibration.
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        juce::ComboBox* factory = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* combo = dynamic_cast<juce::ComboBox*> (child);
                combo != nullptr && combo->getNumItems() >= 2
                    && combo->getItemText (0) == "Natural"
                    && combo->getItemText (1) == "Pitch Center")
            {
                factory = combo;
                break;
            }
        if (factory == nullptr)
        {
            std::cerr << "factory workflow menu was not discoverable\n";
            return 1;
        }
        const int midiIndex = factory->indexOfItemId (factory->getItemId (6));
        factory->setSelectedItemIndex (midiIndex, juce::sendNotificationSync);
        if (apvts.getRawParameterValue ("midiTarget")->load() < 0.5f)
        {
            std::cerr << "MIDI Directed workflow did not engage MIDI targeting\n";
            return 1;
        }
        setParameter (apvts, "capturePitchEdits", 1.0f);
        setParameter (apvts, "usePitchEdits", 1.0f);
        setParameter (apvts, "graphTimeCorrection", 1.0f);
        setParameter (apvts, "xposeMode", 1.0f);
        setParameter (apvts, "polyphonic", 1.0f);
        setParameter (apvts, "addVibrato", 75.0f);
        factory->setSelectedItemIndex (0, juce::sendNotificationSync);
        if (apvts.getRawParameterValue ("midiTarget")->load() > 0.5f
            || apvts.getRawParameterValue ("capturePitchEdits")->load() > 0.5f
            || apvts.getRawParameterValue ("usePitchEdits")->load() > 0.5f
            || apvts.getRawParameterValue ("graphTimeCorrection")->load() > 0.5f
            || apvts.getRawParameterValue ("xposeMode")->load() > 0.5f
            || apvts.getRawParameterValue ("polyphonic")->load() > 0.5f
            || apvts.getRawParameterValue ("addVibrato")->load() > 0.01f
            || std::abs (apvts.getRawParameterValue ("amount")->load() - 0.86f) > 0.001f)
        {
            std::cerr << "Natural workflow inherited an incompatible advanced mode\n";
            return 1;
        }
        std::cout << "  factoryWorkflows=PASS (deterministic mode reset + calibrated Natural)\n";
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
    // Assert on what proves the engine ran, not on obsolete selector copy: the
    // status must identify the single release engine or the fallback guide.
    if (! (status.containsIgnoreCase ("Studio HQ") || status.containsIgnoreCase ("fallback")))
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

    // --- AutoKey confidence gate ---------------------------------------------
    //  REGRESSION PROTECTION FOR A SHIPPED DEFECT. The key detector computed a
    //  Krumhansl margin on every call and discarded it, so material with no
    //  tonal content still produced a key on the session bus that Harmony,
    //  Maqam, Scaler and every other follower consumed as session truth.
    //
    //  Two properties are asserted, because the fix is only correct if BOTH
    //  hold: a real key must clear the gate, and noise must not. Testing only
    //  the second would pass trivially if the gate rejected everything.
    {
        auto runKey = [&] (const juce::AudioBuffer<float>& in)
        {
            SattariAutoPitchAudioProcessor p;
            auto& a = p.parameters();
            setParameter (a, "autoKey", 1.0f);
            setParameter (a, "followSongKey", 0.0f);
            setParameter (a, "enableCorrection", 1.0f);
            p.setNonRealtime (true);
            //  Drive it through the SHARED harness rather than a hand-rolled
            //  block loop. The first attempt fed a mono buffer straight to
            //  processBlock and segfaulted inside a channel-summing loop --
            //  Auto Pitch expects stereo, and renderThrough already handles the
            //  channel adaptation every other caller needs.
            (void) sattari::harness::renderThrough (p, in, 48000.0, 512);
            return p.currentKeyOrigin();
        };

        // A clean tonal melody: must CLEAR the gate, or the fix has broken
        // ordinary key following.
        juce::AudioBuffer<float> tonal (1, 48000 * 6);
        {
            static const int deg[7] = { 0, 2, 4, 5, 7, 9, 11 };
            static const int seq[] = { 0,2,4,2,0, 4,5,4,2,0, 0,2,4,4,2,0, 4,2,0,0 };
            float* d = tonal.getWritePointer (0);
            double phase = 0.0;
            const int noteLen = (int) (48000 * 0.26);
            for (int i = 0; i < tonal.getNumSamples(); ++i)
            {
                const int midi = 60 + deg[seq[(i / noteLen) % 20]];
                const double hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
                d[i] = 0.4f * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * hz / 48000.0;
            }
        }

        // White noise: no tonal content whatsoever, so any key is a guess and
        // the gate MUST withhold it.
        juce::AudioBuffer<float> noise (1, 48000 * 6);
        {
            juce::Random rng (20260810);
            float* d = noise.getWritePointer (0);
            for (int i = 0; i < noise.getNumSamples(); ++i)
                d[i] = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
        }

        const auto tonalKey = runKey (tonal);
        const auto noiseKey = runKey (noise);
        const float gate = SattariAutoPitchAudioProcessor::kMinBusKeyConfidence;

        if (! (tonalKey.confidence >= gate))
        {
            std::cerr << "AutoKey gate rejects a clean tonal melody: margin "
                      << tonalKey.confidence << " < " << gate << "\n";
            return 1;
        }
        if (! (noiseKey.confidence < gate))
        {
            std::cerr << "AutoKey gate ACCEPTS white noise as a key: margin "
                      << noiseKey.confidence << " >= " << gate << "\n";
            return 1;
        }
        if (noiseKey.root != 9 || noiseKey.scale != 1)
        {
            std::cerr << "AutoKey No Result did not retain the manual A minor fallback: "
                      << noiseKey.root << "/" << noiseKey.scale << "\n";
            return 1;
        }
        std::cout << "  autoKeyGate=tonal " << tonalKey.confidence
                  << " >= " << gate << " > noise " << noiseKey.confidence
                  << " (manual fallback retained)\n";
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
