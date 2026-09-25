#include "PluginProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>
#include <sattari_audio_core/SattariRenderHarness.h>

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

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

double midiToHz (int midi)
{
    return 440.0 * std::pow (2.0, ((double) midi - 69.0) / 12.0);
}

double hzToMidi (double hz)
{
    return 69.0 + 12.0 * std::log2 (hz / 440.0);
}

double estimateFrequencyFromPositiveCrossings (const std::vector<float>& samples, double sampleRate)
{
    std::vector<double> crossings;
    crossings.reserve (samples.size() / 80);

    for (size_t i = 1; i < samples.size(); ++i)
    {
        const double prev = samples[i - 1];
        const double cur = samples[i];
        if (prev < 0.0 && cur >= 0.0)
        {
            const double denom = cur - prev;
            const double frac = std::abs (denom) > 1.0e-12 ? -prev / denom : 0.0;
            crossings.push_back ((double) (i - 1) + frac);
        }
    }

    if (crossings.size() < 2)
        return 0.0;

    double periodSum = 0.0;
    for (size_t i = 1; i < crossings.size(); ++i)
        periodSum += crossings[i] - crossings[i - 1];

    const double period = periodSum / (double) (crossings.size() - 1);
    return period > 0.0 ? sampleRate / period : 0.0;
}

// keySetup: 0 = manual C chromatic (the historical fixture), 1 = AutoKey on with the
// manual key left untouched, 2 = AutoKey on with an explicitly chosen C major.
double renderClassicStrongB3Plus45ErrorCents (bool productionLiveMode, int keySetup = 0)
{
    constexpr int blockSize = 512;
    constexpr int blocks = 170;
    constexpr double sampleRate = 48000.0;
    constexpr int targetMidi = 59; // B3
    const double inputFrequency = midiToHz (targetMidi) * std::pow (2.0, 45.0 / 1200.0);

    SattariAutoPitchAudioProcessor processor;
    auto& apvts = processor.parameters();
    setParameter (apvts, "liveMode", productionLiveMode ? 1.0f : 0.0f);
    setParameter (apvts, "liveProfile", 0.0f);
    setParameter (apvts, "extremeLive", productionLiveMode ? 1.0f : 0.0f);
    if (keySetup != 1)
    {
        setParameter (apvts, "keyRoot", 0.0f); // C
        setParameter (apvts, "scale", keySetup == 2 ? 0.0f : 2.0f);     // major / chromatic
        setParameter (apvts, "scaleMode", keySetup == 2 ? 0.0f : 2.0f);
    }
    setParameter (apvts, "autoKey", keySetup != 0 ? 1.0f : 0.0f);
    setParameter (apvts, "style", 1.0f); // Classic style must hard-correct.
    setParameter (apvts, "amount", 1.0f);
    setParameter (apvts, "speed", 1.0f);
    setParameter (apvts, "enableCorrection", 1.0f);
    setParameter (apvts, "doNoHarm", 0.0f);
    setParameter (apvts, "formantPreserve", 1.0f);
    setParameter (apvts, "classicMode", 0.0f); // Style alone must be enough.
    processor.prepareToPlay (sampleRate, blockSize);

    std::vector<float> steadyOutput;
    steadyOutput.reserve (8192);

    double phase = 0.0;
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float sample = 0.18f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * inputFrequency / sampleRate;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, i, sample);
        }

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        if (block >= blocks - 16)
            for (int i = 0; i < blockSize; ++i)
                steadyOutput.push_back (buffer.getSample (0, i));
    }

    const double outputFrequency = estimateFrequencyFromPositiveCrossings (steadyOutput, sampleRate);
    if (outputFrequency <= 0.0)
        return 999.0;

    return std::abs ((hzToMidi (outputFrequency) - (double) targetMidi) * 100.0);
}

// Correction-strength parity (release gate): AutoKey decides WHICH note set is legal, never HOW
// HARD a note is pulled. Each AutoKey configuration is rendered next to the non-AutoKey setup that
// produces the same effective key; the steady-state output must match.
//   keySource 0 manual C chromatic      | 2 AutoKey, key untouched (No Result -> Chromatic)
//   keySource 1 manual C major          | 3 AutoKey, explicit C major (No Result -> C major)
//   keySource 5 manual key + melody     | 4 AutoKey after a C-major melody (detected key)
// The manual twins use keyRoot/keyScale (default C major); for 4 vs 5 the twin is given exactly
// the key AutoKey ended up detecting, so the pair differs only in where the key came from.
struct ParityRender { double errorCents; float keyConfidence; int keyRoot; int keyScale; };

ParityRender renderSteadyToneParity (int targetMidi, double detuneCents, int keySource,
                                     int style, float amount, bool productionLiveMode,
                                     int keyRoot = 0, int keyScale = 0)
{
    constexpr int blockSize = 512;
    constexpr double sampleRate = 48000.0;
    const bool prefix = keySource == 4 || keySource == 5;
    const int prefixSamples = prefix ? (int) (sampleRate * 6.0) : 0;
    const int toneBlocks = 235;
    const int blocks = (prefixSamples + blockSize - 1) / blockSize + toneBlocks;
    const double toneHz = midiToHz (targetMidi) * std::pow (2.0, detuneCents / 1200.0);

    SattariAutoPitchAudioProcessor processor;
    auto& apvts = processor.parameters();
    setParameter (apvts, "liveMode", productionLiveMode ? 1.0f : 0.0f);
    setParameter (apvts, "liveProfile", 0.0f);
    setParameter (apvts, "extremeLive", productionLiveMode ? 1.0f : 0.0f);
    if (keySource == 0 || keySource == 1 || keySource == 3 || keySource == 5)
    {
        const float sc = keySource == 0 ? 2.0f : (float) keyScale;
        setParameter (apvts, "keyRoot", keySource == 0 ? 0.0f : (float) keyRoot);
        setParameter (apvts, "scale", sc);
        setParameter (apvts, "scaleMode", sc);
    }
    setParameter (apvts, "autoKey", (keySource == 2 || keySource == 3 || keySource == 4) ? 1.0f : 0.0f);
    setParameter (apvts, "followSongKey", 0.0f);
    setParameter (apvts, "style", (float) style);
    setParameter (apvts, "amount", amount);
    setParameter (apvts, "speed", 1.0f);
    setParameter (apvts, "enableCorrection", 1.0f);
    setParameter (apvts, "doNoHarm", 0.0f);
    setParameter (apvts, "formantPreserve", 1.0f);
    setParameter (apvts, "classicMode", 0.0f);
    processor.prepareToPlay (sampleRate, blockSize);

    static const int deg[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int seq[] = { 0,2,4,2,0, 4,5,4,2,0, 0,2,4,4,2,0, 4,2,0,0 };
    const int noteLen = (int) (sampleRate * 0.26);
    std::vector<float> steadyOutput;
    double phase = 0.0;
    long n = 0;
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i, ++n)
        {
            const double hz = n < prefixSamples
                ? midiToHz (60 + deg[seq[(n / noteLen) % 20]])
                : toneHz;
            const float sample = 0.18f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * hz / sampleRate;
            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample (channel, i, sample);
        }
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        if (block >= blocks - 16)
            for (int i = 0; i < blockSize; ++i)
                steadyOutput.push_back (buffer.getSample (0, i));
    }

    const auto origin = processor.currentKeyOrigin();
    const double outHz = estimateFrequencyFromPositiveCrossings (steadyOutput, sampleRate);
    return { outHz > 0.0 ? std::abs ((hzToMidi (outHz) - (double) targetMidi) * 100.0) : 999.0,
             origin.confidence, origin.root, origin.scale };
}

// A note sung +42 c sharp with +-20 c vibrato crosses the half-semitone line
// every cycle. Classic must still pull its centre to the intended note instead of
// flipping the target between B3 and C4 frame by frame.
double renderVibratoNearBoundaryErrorCents (bool targetHysteresis)
{
    constexpr int blockSize = 512;
    constexpr int blocks = 240;
    constexpr double sampleRate = 48000.0;
    constexpr int targetMidi = 59; // B3
    const double centreHz = midiToHz (targetMidi) * std::pow (2.0, 42.0 / 1200.0);

    SattariAutoPitchAudioProcessor processor;
    processor.setTargetHysteresisEnabled (targetHysteresis);
    auto& apvts = processor.parameters();
    setParameter (apvts, "keyRoot", 0.0f);
    setParameter (apvts, "scale", 2.0f);      // chromatic
    setParameter (apvts, "scaleMode", 2.0f);
    setParameter (apvts, "style", 1.0f);      // Classic
    setParameter (apvts, "amount", 1.0f);
    setParameter (apvts, "speed", 1.0f);
    setParameter (apvts, "enableCorrection", 1.0f);
    setParameter (apvts, "doNoHarm", 0.0f);
    setParameter (apvts, "classicMode", 0.0f);
    processor.prepareToPlay (sampleRate, blockSize);

    std::vector<float> tail;
    double phase = 0.0;
    int n = 0;
    for (int block = 0; block < blocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i, ++n)
        {
            const double cents = 20.0 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * n / sampleRate);
            const float sample = 0.18f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * centreHz * std::pow (2.0, cents / 1200.0) / sampleRate;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.setSample (channel, i, sample);
        }
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        if (block >= blocks - 110)   // ~1.2 s = six vibrato cycles
            for (int i = 0; i < blockSize; ++i)
                tail.push_back (buffer.getSample (0, i));
    }
    const double hz = estimateFrequencyFromPositiveCrossings (tail, sampleRate);
    return hz > 0.0 ? std::abs ((hzToMidi (hz) - (double) targetMidi) * 100.0) : 999.0;
}

// A deterministic, never-repeating-within-a-second test signal, so any stale audio
// replayed from an earlier moment is caught sample for sample.
float bypassProbeSample (juce::int64 n)
{
    const double t = (double) n / 48000.0;
    return (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * 173.0 * t)
                  + 0.1 * std::sin (juce::MathConstants<double>::twoPi * 0.37 * t * t * 480.0));
}

// Host bypass must be the dry input delayed by the reported latency at EVERY
// sample, including the first latency's worth after bypass is re-engaged. The
// delay ring used to be written only while bypassed, so re-engaging replayed the
// tail of the previous bypass. Returns the worst sample error.
double bypassReengageMaxError (int blockSize, int& latencyOut)
{
    SattariAutoPitchAudioProcessor processor;
    processor.prepareToPlay (48000.0, blockSize);
    const int latency = processor.getLatencySamples();
    latencyOut = latency;
    if (latency <= 0)
        return 999.0;

    juce::int64 n = 0;
    double worst = 0.0;
    auto run = [&] (int blocks, bool bypassed, bool check)
    {
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < blockSize; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample (ch, i, bypassProbeSample (n + i));
            juce::MidiBuffer midi;
            if (bypassed) processor.processBlockBypassed (buffer, midi);
            else          processor.processBlock (buffer, midi);
            if (check)
                for (int i = 0; i < blockSize; ++i)
                {
                    const juce::int64 src = n + i - latency;
                    const float expected = src >= 0 ? bypassProbeSample (src) : 0.0f;
                    for (int ch = 0; ch < 2; ++ch)
                        worst = std::max (worst, (double) std::abs (buffer.getSample (ch, i) - expected));
                }
            n += blockSize;
        }
    };
    const int blocksPerSecond = 48000 / blockSize;
    run (blocksPerSecond, true, true);        // first bypass
    run (blocksPerSecond, false, false);      // processing
    run (blocksPerSecond / 2, true, true);    // bypass re-engaged
    return worst;
}

// Seconds from a step in sung pitch until the correction target (debug frame)
// reaches `wantedTarget`; -1 when it never does. The tone is harmonic so the
// neural detector sees a voice-like spectrum.
struct TargetStepRun
{
    double seconds = -1.0;
    bool   neuralLoaded = true;
};

TargetStepRun targetStepSeconds (bool neural, bool hold, int blockSize,
                                 double fromMidi, double toMidi, int wantedTarget)
{
    constexpr double sampleRate = 48000.0;
    SattariAutoPitchAudioProcessor processor;
    processor.setTargetHysteresisEnabled (hold);
    processor.setNonRealtime (true);   // CREPE runs synchronously: deterministic
    auto& apvts = processor.parameters();
    setParameter (apvts, "keyRoot", 0.0f);
    setParameter (apvts, "scale", 2.0f);      // chromatic
    setParameter (apvts, "scaleMode", 2.0f);
    setParameter (apvts, "style", 1.0f);      // Classic
    setParameter (apvts, "amount", 1.0f);
    setParameter (apvts, "speed", 1.0f);
    setParameter (apvts, "enableCorrection", 1.0f);
    setParameter (apvts, "doNoHarm", 0.0f);
    setParameter (apvts, "neuralPitch", neural ? 1.0f : 0.0f);
    processor.prepareToPlay (sampleRate, blockSize);

    TargetStepRun result;
    result.neuralLoaded = ! neural || processor.neuralModelPresent();
    if (! result.neuralLoaded)
        return result;

    const int stepAt = (int) (0.6 * sampleRate);
    const int total  = (int) (1.2 * sampleRate);
    double phase = 0.0;
    for (int start = 0; start < total; start += blockSize)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const double midi = (start + i) < stepAt ? fromMidi : toMidi;
            const float s = (float) (0.16 * std::sin (phase) + 0.08 * std::sin (2.0 * phase)
                                   + 0.04 * std::sin (3.0 * phase));
            phase += juce::MathConstants<double>::twoPi * 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) / sampleRate;
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, s);
        }
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        const int end = start + blockSize;
        const auto debug = processor.getDebugFrame();
        if (end > stepAt && debug.voiced && (int) std::llround (debug.targetMidi) == wantedTarget)
        {
            result.seconds = (double) (end - stepAt) / sampleRate;
            return result;
        }
    }
    return result;
}

// Seconds from a key / Edit-Notes change until the target leaves a note the new
// rules no longer allow. The sung pitch never moves, so only the hold can delay it.
double retargetAfterRuleChangeSeconds (bool changeMask, int blockSize)
{
    constexpr double sampleRate = 48000.0;
    SattariAutoPitchAudioProcessor processor;
    auto& apvts = processor.parameters();
    setParameter (apvts, "keyRoot", 0.0f);    // C major
    setParameter (apvts, "scale", 0.0f);
    setParameter (apvts, "scaleMode", 0.0f);
    setParameter (apvts, "style", 1.0f);
    setParameter (apvts, "amount", 1.0f);
    setParameter (apvts, "speed", 1.0f);
    setParameter (apvts, "enableCorrection", 1.0f);
    setParameter (apvts, "doNoHarm", 0.0f);
    processor.prepareToPlay (sampleRate, blockSize);

    const double sungHz = midiToHz (64) * std::pow (2.0, 30.0 / 1200.0);   // E4 +30 c
    const int changeAt = (int) (0.6 * sampleRate);
    const int total    = (int) (1.2 * sampleRate);
    double phase = 0.0;
    bool changed = false;
    for (int start = 0; start < total; start += blockSize)
    {
        if (! changed && start >= changeAt)
        {
            // Target must be E4 before the change, or the test proves nothing.
            if ((int) std::llround (processor.getDebugFrame().targetMidi) != 64)
                return -2.0;
            if (changeMask) setParameter (apvts, "enabledNotes", (float) (4095 & ~(1 << 4)));   // block E
            else            setParameter (apvts, "keyRoot", 3.0f);                             // Eb major: no E
            changed = true;
        }
        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const float s = (float) (0.16 * std::sin (phase) + 0.08 * std::sin (2.0 * phase));
            phase += juce::MathConstants<double>::twoPi * sungHz / sampleRate;
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, s);
        }
        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);
        if (changed && (int) std::llround (processor.getDebugFrame().targetMidi) == 65)   // F4
            return (double) (start + blockSize - changeAt) / sampleRate;
    }
    return -1.0;
}

double tonePower (const std::vector<float>& audio, int start, double sampleRate, double hz)
{
    double real = 0.0, imag = 0.0, phase = 0.0;
    const double step = juce::MathConstants<double>::twoPi * hz / sampleRate;
    for (int i = start; i < (int) audio.size(); ++i)
    {
        const double sample = audio[(size_t) i];
        real += sample * std::cos (phase);
        imag -= sample * std::sin (phase);
        phase += step;
    }
    return real * real + imag * imag;
}

double strongestNear (const std::vector<float>& audio, int start, double sampleRate,
                      double centreHz, double radiusHz)
{
    double bestHz = centreHz, bestPower = -1.0;
    for (double hz = centreHz - radiusHz; hz <= centreHz + radiusHz; hz += 0.25)
    {
        const double power = tonePower (audio, start, sampleRate, hz);
        if (power > bestPower) { bestPower = power; bestHz = hz; }
    }
    return bestHz;
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

bool headerHasVisibleContent (juce::Component& editor)
{
    const auto headerBounds = juce::Rectangle<int> (0, 0, editor.getWidth(),
                                                     juce::jmin (76, editor.getHeight()));
    const auto image = editor.createComponentSnapshot (headerBounds, true);
    int brightPixels = 0;
    for (int y = 0; y < image.getHeight(); y += 2)
        for (int x = 0; x < image.getWidth(); x += 2)
            if (image.getPixelAt (x, y).getPerceivedBrightness() > 0.48f)
                ++brightPixels;
    return brightPixels >= 80;
}
}

int main (int argc, char** argv)
{
    // Hosts may tear an instance down without ever preparing it (plugin scan,
    // insert-then-remove). This used to segfault inside releaseResources().
    for (int i = 0; i < 8; ++i)
    {
        SattariAutoPitchAudioProcessor unprepared;
        unprepared.releaseResources();
    }

    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    // Keep every SessionReader used by this executable away from an open DAW.
    const auto contractSession = "auto-pitch-contract-" + juce::String (
        sattari::xplugin::currentTimeMs());
   #if JUCE_WINDOWS
    _putenv_s ("SATTARI_SESSION_ID", contractSession.toRawUTF8());
   #else
    setenv ("SATTARI_SESSION_ID", contractSession.toRawUTF8(), 1);
   #endif

    if (argc == 3
        && (std::strcmp (argv[1], "--render-ui") == 0
            || std::strcmp (argv[1], "--render-ui-min") == 0
            || std::strcmp (argv[1], "--render-ui-pro") == 0
            || std::strcmp (argv[1], "--render-ui-key") == 0
            || std::strcmp (argv[1], "--render-ui-harmony") == 0
            || std::strcmp (argv[1], "--render-ui-edit") == 0))
    {
        SattariAutoPitchAudioProcessor processor;
        const bool minimumSize = std::strcmp (argv[1], "--render-ui-min") == 0;
        //  The tab strip is gone — one page now. "--render-ui-pro" renders with the
        //  ADVANCED drawer open (the old PRO content), and "--render-ui-edit" is the
        //  same page as the default since the graph is always visible.
        const bool proTab = std::strcmp (argv[1], "--render-ui-pro") == 0;
        const bool keyWs  = std::strcmp (argv[1], "--render-ui-key") == 0;
        const bool harmWs = std::strcmp (argv[1], "--render-ui-harmony") == 0;
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
            if (harmWs)
                ok = clickButtonNamed (*editor, "HARMONY");
            if (editWs)
                ok = clickButtonNamed (*editor, "EDIT");
            if (ok && ! headerHasVisibleContent (*editor))
            {
                std::fprintf (stderr, "FAIL render: persistent header is blank\n");
                ok = false;
            }
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
        "addVibrato", "vibratoRate", "vibratoOnset", "liveProfile", "extremeLive"
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
            || apvts.getRawParameterValue ("liveMode")->load() > 0.5f
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
        // Live graph edits must retain their exact octave-aware target instead of
        // being collapsed back to a pitch-class mask. This is the causal planner
        // contract used by processWithLiveEngine for per-object note overrides.
        sattari::dsp::LiveCorrectionPlanner planner;
        planner.prepare (48000.0);
        sattari::dsp::LivePitchTracker::Result pitch;
        pitch.frequencyHz = 220.0; // A3 / MIDI 57
        pitch.confidence = 1.0f;
        pitch.rms = 0.2f;
        pitch.voiced = true;
        sattari::dsp::LiveCorrectionPlanner::Settings settings;
        settings.forcedTargetMidi = 58;
        settings.retuneMilliseconds = 1.5;
        settings.transitionAmount = 1.0;
        planner.update (pitch, 512, settings);
        if (planner.getTargetMidi() != 58
            || std::abs (planner.getCorrectionCents() - 100.0) > 1.0)
        {
            std::cerr << "live graph target was not applied exactly\n";
            return 1;
        }
        settings.forcedTargetMidi = -1;
        planner.update (pitch, 512, settings);
        if (planner.getTargetMidi() != 57)
        {
            std::cerr << "live planner did not release an explicit graph target\n";
            return 1;
        }

        // The neural/fused guidance hook must be musically active, not a CPU-only
        // switch. One block is intentionally shorter than the causal tracker's
        // acquisition window, so only the supplied guide can establish A4.
        sattari::dsp::LivePitchEngine guided;
        guided.prepare (48000.0, 512, 128.0, 1100.0);
        std::array<float, 512> in {}, out {};
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.2f * std::sin ((float) (juce::MathConstants<double>::twoPi
                                 * 445.0 * (double) i / 48000.0));
        sattari::dsp::LivePitchTracker::Result guide;
        guide.frequencyHz = 445.0;
        guide.periodSamples = 48000.0 / guide.frequencyHz;
        guide.confidence = 0.98f;
        guide.rms = 0.14f;
        guide.voiced = true;
        guided.processCorrected (in.data(), out.data(), (int) in.size(), settings, &guide);
        if (guided.getTargetMidi() != 69
            || std::abs (guided.getPitchResult().frequencyHz - 445.0) > 0.01)
        {
            std::cerr << "fused pitch guidance did not reach the live planner\n";
            return 1;
        }
        std::cout << "  liveGraphTarget=PASS (exact target + release + fused guidance)\n";
    }

    {
        // Audible harmony contract: every object owns a continuous shifter and
        // reaches its requested note from the same lead. Rendering at 64 and 512
        // samples must also agree, proving host callback boundaries do not reset
        // pitch phase (the failure mode of the retired Big Harmony resampler).
        constexpr double sr = 48000.0;
        constexpr int total = 96000;
        auto renderVoice = [=] (int targetMidi, int blockSize)
        {
            sattari::dsp::HarmonyRenderer renderer;
            renderer.prepare (sr, 512, 128.0, 1100.0, 4096);
            std::array<sattari::dsp::HarmonyRenderer::Voice, 4> voices;
            voices[0].active = true;
            voices[0].targetMidi = targetMidi;
            voices[0].level = 1.0f;
            voices[0].pan = 0.0f;
            sattari::dsp::HarmonyRenderer::Settings settings;
            settings.glideMs = 1.5f;
            settings.humanPitchCents = 0.0f;
            settings.humanTimingMs = 0.0f;
            settings.ensemble = 0.0f;
            settings.lowCutHz = 20.0f;
            settings.highCutHz = 20000.0f;

            std::vector<float> input ((size_t) total), left ((size_t) total), right ((size_t) total);
            double phase = 0.0;
            for (int i = 0; i < total; ++i)
            {
                input[(size_t) i] = 0.22f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 220.0 / sr;
            }
            for (int offset = 0; offset < total; offset += blockSize)
            {
                const int n = juce::jmin (blockSize, total - offset);
                const float* in[] { input.data() + offset };
                renderer.process (in, 1, left.data() + offset, right.data() + offset, n,
                                  57.0, sr / 220.0, true, voices, settings);
            }
            return left;
        };

        const std::array<int, 4> targets { 60, 64, 55, 69 };
        for (int target : targets)
        {
            const auto rendered = renderVoice (target, 64);
            const double expected = 440.0 * std::pow (2.0, ((double) target - 69.0) / 12.0);
            const double measured = strongestNear (rendered, 36000, sr, expected, 12.0);
            const double expectedPower = tonePower (rendered, 36000, sr, measured);
            const double dryPower = tonePower (rendered, 36000, sr, 220.0);
            if (std::abs (1200.0 * std::log2 (measured / expected)) > 35.0)
            {
                std::cerr << "harmony voice missed MIDI " << target
                          << ": measured " << measured << " Hz, expected " << expected << " Hz\n";
                return 1;
            }
            if (target != 57 && expectedPower < 1.5 * dryPower)
            {
                std::cerr << "harmony target is masked by dry fundamental at MIDI " << target
                          << ": target/dry power=" << expectedPower / std::max (1.0e-12, dryPower) << "\n";
                return 1;
            }
        }

        const auto smallBlocks = renderVoice (64, 64);
        const auto largeBlocks = renderVoice (64, 512);
        double smallEnergy = 0.0, largeEnergy = 0.0;
        double smallPeakJump = 0.0, largePeakJump = 0.0;
        int compared = 0;
        for (int i = 36000; i < total; ++i)
        {
            smallEnergy += (double) smallBlocks[(size_t) i] * smallBlocks[(size_t) i];
            largeEnergy += (double) largeBlocks[(size_t) i] * largeBlocks[(size_t) i];
            ++compared;
            if (i % 64 == 0)
                smallPeakJump = std::max (smallPeakJump,
                    std::abs ((double) smallBlocks[(size_t) i]
                            - (double) smallBlocks[(size_t) (i - 1)]));
            if (i % 512 == 0)
                largePeakJump = std::max (largePeakJump,
                    std::abs ((double) largeBlocks[(size_t) i]
                            - (double) largeBlocks[(size_t) (i - 1)]));
        }
        const double smallRms = std::sqrt (smallEnergy / (double) compared);
        const double largeRms = std::sqrt (largeEnergy / (double) compared);
        const double smallHz = strongestNear (smallBlocks, 36000, sr, 329.63, 12.0);
        const double largeHz = strongestNear (largeBlocks, 36000, sr, 329.63, 12.0);
        const double pitchDeltaCents = std::abs (1200.0 * std::log2 (smallHz / largeHz));
        const double energyRatio = smallRms / std::max (1.0e-12, largeRms);
        if (pitchDeltaCents > 5.0 || energyRatio < 0.85 || energyRatio > 1.18
            || smallPeakJump > 0.12 || largePeakJump > 0.12)
        {
            std::cerr << "harmony block continuity failed: pitchDelta=" << pitchDeltaCents
                      << " ct energyRatio=" << energyRatio
                      << " boundaryJumps=" << smallPeakJump << "/" << largePeakJump << "\n";
            return 1;
        }
        std::cout << "  harmonyRenderer=PASS (4 target notes, 64/512 continuity, max boundary jump "
                  << std::max (smallPeakJump, largePeakJump) << ")\n";
    }

    {
        // Processor integration: the Harmony workspace parameters must reach the
        // audio output, and its public readout must report the same note that the
        // renderer sings. Voice-only makes the spectral assertion unambiguous.
        SattariAutoPitchAudioProcessor harmony;
        auto& params = harmony.parameters();
        setParameter (params, "liveMode", 1.0f);
        setParameter (params, "liveProfile", 0.0f);
        setParameter (params, "keyRoot", 0.0f);
        setParameter (params, "scale", 0.0f);
        setParameter (params, "scaleMode", 0.0f);
        setParameter (params, "enableCorrection", 1.0f);
        setParameter (params, "amount", 1.0f);
        setParameter (params, "speed", 1.0f);
        setParameter (params, "doNoHarm", 0.0f);
        setParameter (params, "harmonyMode", 1.0f); // interval
        setParameter (params, "h1Enable", 1.0f);
        setParameter (params, "h1Degrees", 2.0f);
        setParameter (params, "h1Level", 0.0f);
        setParameter (params, "h1Pan", 0.0f);
        setParameter (params, "h1Delay", 0.0f);
        setParameter (params, "h1Detune", 0.0f);
        setParameter (params, "harmonyMix", 1.0f);
        setParameter (params, "harmonyVoiceOnly", 1.0f);
        setParameter (params, "harmonyHumanPitch", 0.0f);
        setParameter (params, "harmonyHumanTime", 0.0f);
        setParameter (params, "harmonyEnsemble", 0.0f);
        harmony.prepareToPlay (48000.0, 64);

        std::vector<float> output (96000, 0.0f);
        double phase = 0.0;
        for (int offset = 0; offset < (int) output.size(); offset += 64)
        {
            juce::AudioBuffer<float> block (2, 64);
            for (int i = 0; i < 64; ++i)
            {
                const float sample = 0.20f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
                block.setSample (0, i, sample);
                block.setSample (1, i, sample);
            }
            juce::MidiBuffer midi;
            harmony.processBlock (block, midi);
            std::copy_n (block.getReadPointer (0), 64, output.data() + offset);
        }
        const auto readout = harmony.planHarmonyNow();
        if (! readout.valid || ! readout.active[0] || readout.midi[0] != 60)
        {
            std::cerr << "processor harmony planner/render integration target mismatch\n";
            return 1;
        }
        const double targetHz = 261.625565;
        const double measured = strongestNear (output, 36000, 48000.0, targetHz, 10.0);
        const double targetPower = tonePower (output, 36000, 48000.0, measured);
        const double sourcePower = tonePower (output, 36000, 48000.0, 220.0);
        if (std::abs (1200.0 * std::log2 (measured / targetHz)) > 35.0
            || targetPower < 1.5 * sourcePower)
        {
            std::cerr << "processor harmony output failed: measured=" << measured
                      << " target/source power=" << targetPower / std::max (1.0e-12, sourcePower) << "\n";
            return 1;
        }

        // Range -> Ultra -> Range must not resume an old delay-ring moment. A
        // freshly selected bank begins with its own qualified latency, hence the
        // first block after returning to Range is silent in voice-only mode.
        auto processHarmonyBlock = [&]
        {
            juce::AudioBuffer<float> block (2, 64);
            for (int i = 0; i < 64; ++i)
            {
                const float sample = 0.20f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
                block.setSample (0, i, sample);
                block.setSample (1, i, sample);
            }
            juce::MidiBuffer midi;
            harmony.processBlock (block, midi);
            return block;
        };
        setParameter (params, "liveProfile", 1.0f);
        for (int i = 0; i < 30; ++i) (void) processHarmonyBlock();
        setParameter (params, "liveProfile", 0.0f);
        for (int i = 0; i < 30; ++i) (void) processHarmonyBlock();
        setParameter (params, "liveProfile", 1.0f);
        const auto switched = processHarmonyBlock();
        if (switched.getMagnitude (0, switched.getNumSamples()) > 0.001f)
        {
            std::cerr << "harmony renderer resumed stale audio after profile switch\n";
            return 1;
        }
        std::cout << "  harmonyIntegration=PASS (H1 C4 audible + readout matched; bank switch clean)\n";
    }

    {
        SattariAutoPitchAudioProcessor extreme;
        setParameter (extreme.parameters(), "liveMode", 1.0f);
        setParameter (extreme.parameters(), "liveProfile", 0.0f);
        setParameter (extreme.parameters(), "extremeLive", 1.0f);
        extreme.prepareToPlay (48000.0, 512);
        SattariAutoPitchAudioProcessor ultra;
        setParameter (ultra.parameters(), "liveMode", 1.0f);
        setParameter (ultra.parameters(), "liveProfile", 0.0f);
        setParameter (ultra.parameters(), "extremeLive", 0.0f);
        ultra.prepareToPlay (48000.0, 512);
        SattariAutoPitchAudioProcessor extended;
        setParameter (extended.parameters(), "liveMode", 1.0f);
        setParameter (extended.parameters(), "liveProfile", 1.0f);
        extended.prepareToPlay (48000.0, 512);
        if (extreme.getLatencySamples() != 96 || ultra.getLatencySamples() != 571
            || extended.getLatencySamples() != 1037)
        {
            std::cerr << "live profile latency contract changed: extreme="
                      << extreme.getLatencySamples() << " ultra="
                      << ultra.getLatencySamples() << " range="
                      << extended.getLatencySamples() << "\n";
            return 1;
        }
        std::cout << "  liveProfiles=PASS (Extreme 96, Ultra 571, Range 1037 smp @ 48 kHz)\n";
    }

    {
        // Processing delay and correction acquisition are different contracts.
        // Extreme must return audio in 2 ms AND establish a useful correction
        // promptly after a fresh voiced onset.
        SattariAutoPitchAudioProcessor extreme;
        auto& params = extreme.parameters();
        setParameter (params, "liveMode", 1.0f);
        setParameter (params, "liveProfile", 0.0f);
        setParameter (params, "extremeLive", 1.0f);
        setParameter (params, "keyRoot", 9.0f); // A major
        setParameter (params, "scale", 0.0f);
        setParameter (params, "enableCorrection", 1.0f);
        setParameter (params, "amount", 1.0f);
        setParameter (params, "speed", 1.0f);
        setParameter (params, "doNoHarm", 0.0f);
        extreme.prepareToPlay (48000.0, 64);
        int voicedAt = -1, correctedAt = -1;
        double phase = 0.0;
        for (int blockIndex = 0; blockIndex < 80 && correctedAt < 0; ++blockIndex)
        {
            juce::AudioBuffer<float> block (2, 64);
            for (int i = 0; i < 64; ++i)
            {
                const float sample = 0.2f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 456.0 / 48000.0;
                block.setSample (0, i, sample);
                block.setSample (1, i, sample);
            }
            juce::MidiBuffer midi;
            extreme.processBlock (block, midi);
            const auto snapshot = extreme.getLiveSnapshot();
            if (snapshot.voiced && voicedAt < 0) voicedAt = (blockIndex + 1) * 64;
            if (snapshot.voiced && std::abs (snapshot.correctionCents) >= 50.0f)
                correctedAt = (blockIndex + 1) * 64;
        }
        const double voicedMs = 1000.0 * (double) voicedAt / 48000.0;
        const double correctedMs = 1000.0 * (double) correctedAt / 48000.0;
        if (voicedAt < 0 || correctedAt < 0 || voicedMs > 30.0 || correctedMs > 45.0)
        {
            std::cerr << "Extreme correction acquisition too slow: voiced="
                      << voicedMs << " ms correction=" << correctedMs << " ms\n";
            return 1;
        }
        std::cout << "  extremeAcquisition=PASS (voiced " << voicedMs
                  << " ms, >=50 ct correction " << correctedMs << " ms)\n";
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

    const double classicB3ErrorCents = renderClassicStrongB3Plus45ErrorCents (false);
    if (classicB3ErrorCents > 25.0)
    {
        std::cerr << "Classic strong correction missed B3 +45c steady target; absErrorCents="
                  << classicB3ErrorCents << "\n";
        return 1;
    }

    //  AutoKey No Result must correct exactly as hard as the key it falls back to.
    //  A steady tone carries no key, so AutoKey stays below the gate and the note set
    //  is Chromatic (untouched) or the chosen manual key - neither is a guess. The
    //  intent gate used to scale these corrections by the REJECTED detection's
    //  confidence (floor 0.35): +45 c landed ~29 c sharp, and real tenors/basses lost
    //  up to 40 points of notes-within-10-c with AutoKey on (accuracy Round 10).
    const double autoKeyUntouchedB3Cents = renderClassicStrongB3Plus45ErrorCents (false, 1);
    const double autoKeyExplicitB3Cents  = renderClassicStrongB3Plus45ErrorCents (false, 2);
    if (std::abs (autoKeyUntouchedB3Cents - classicB3ErrorCents) > 3.0
        || std::abs (autoKeyExplicitB3Cents - classicB3ErrorCents) > 3.0)
    {
        std::cerr << "AutoKey No Result weakened correction: B3 +45c abs error manual "
                  << classicB3ErrorCents << ", AutoKey untouched " << autoKeyUntouchedB3Cents
                  << ", AutoKey explicit C major " << autoKeyExplicitB3Cents << "\n";
        return 1;
    }

    //  The full parity matrix: low / mid / high voices, hard and gentle correction, Studio and
    //  Live engines, and every key source AutoKey can hand correction. A difference > 1 c between
    //  an AutoKey render and its non-AutoKey twin means AutoKey changed correction strength.
    int parityRenders = 0;
    double parityWorstCents = 0.0;
    {
        struct Case { int midi; int style; float amount; bool live; const char* name; };
        const Case cases[] = {
            { 43, 1, 1.0f, false, "G2 Classic 100%" }, { 59, 1, 1.0f, false, "B3 Classic 100%" },
            { 76, 1, 1.0f, false, "E5 Classic 100%" }, { 43, 0, 0.5f, false, "G2 Natural 50%" },
            { 59, 0, 0.5f, false, "B3 Natural 50%" },  { 76, 0, 0.5f, false, "E5 Natural 50%" },
            { 59, 1, 1.0f, true,  "B3 Classic 100% Live" } };
        const int pairs[3][2] = { { 2, 0 }, { 3, 1 }, { 4, 5 } };
        const char* pairNames[3] = { "AutoKey No Result (untouched) vs manual Chromatic",
                                     "AutoKey No Result (explicit C major) vs manual C major",
                                     "AutoKey detected key vs the same key set manually" };
        for (const auto& c : cases)
            for (int p = 0; p < 3; ++p)
            {
                const auto ak  = renderSteadyToneParity (c.midi, 45.0, pairs[p][0], c.style, c.amount, c.live);
                const auto ref = renderSteadyToneParity (c.midi, 45.0, pairs[p][1], c.style, c.amount, c.live,
                                                         p == 2 ? ak.keyRoot : 0, p == 2 ? ak.keyScale : 0);
                parityRenders += 2;
                if (p == 2 && ! (ak.keyConfidence >= SattariAutoPitchAudioProcessor::kMinBusKeyConfidence
                                 && (ak.keyScale == 0 || ak.keyScale == 1)))
                {
                    std::cerr << "Parity fixture did not reach a confident detected key (" << c.name
                              << "): root " << ak.keyRoot << " scale " << ak.keyScale
                              << " confidence " << ak.keyConfidence << "\n";
                    return 1;
                }
                const double diff = std::abs (ak.errorCents - ref.errorCents);
                parityWorstCents = juce::jmax (parityWorstCents, diff);
                if (diff > 1.0 || ref.errorCents > 900.0)
                {
                    std::cerr << "AutoKey changed correction strength: " << c.name << ", " << pairNames[p]
                              << ": abs error " << ak.errorCents << " c vs " << ref.errorCents << " c\n";
                    return 1;
                }
            }
    }

    const double vibratoBoundaryErrorCents = renderVibratoNearBoundaryErrorCents (true);
    const double vibratoBoundaryNoHoldCents = renderVibratoNearBoundaryErrorCents (false);
    if (vibratoBoundaryErrorCents > 12.0)
    {
        std::cerr << "Classic flipped targets on a +42c note with vibrato; absErrorCents="
                  << vibratoBoundaryErrorCents << " (without target hold: " << vibratoBoundaryNoHoldCents << ")\n";
        return 1;
    }

    // --- Host bypass re-engage -----------------------------------------------
    for (const int bypassBlock : { 64, 512 })
    {
        int latency = 0;
        const double err = bypassReengageMaxError (bypassBlock, latency);
        if (err > 1.0e-6)
        {
            std::cerr << "bypass re-engage replayed stale audio (block " << bypassBlock
                      << ", latency " << latency << "): max error " << err << "\n";
            return 1;
        }
    }
    std::cout << "  bypassReengage=PASS (dry input delayed by latency at every sample, 64 + 512)\n";

    // --- Target hold timing, YIN and neural, small and large blocks ----------
    //  A +62 c step (A3 -> A3+62 c) is nearer A#3 by less than the 0.5 st switch
    //  margin, so only the 120 ms hold may move the target. The hold must last
    //  the same wall time at every block size and with Neural Pitch on; it used
    //  to be advanced once per YIN hop AND once per block on the fused frame.
    {
        std::string holdReport;
        for (const bool neural : { false, true })
            for (const int holdBlock : { 64, 512 })
            {
                const auto held = targetStepSeconds (neural, true,  holdBlock, 57.0, 57.62, 58);
                const auto free = targetStepSeconds (neural, false, holdBlock, 57.0, 57.62, 58);
                if (! held.neuralLoaded)
                {
                    std::cerr << "Neural Pitch hold test: CREPE model not loaded\n";
                    return 1;
                }
                const double holdSec = held.seconds - free.seconds;
                if (held.seconds < 0.0 || free.seconds < 0.0 || holdSec < 0.095 || holdSec > 0.150)
                {
                    std::cerr << "target hold is not 120 ms (neural " << neural << ", block " << holdBlock
                              << "): switch " << held.seconds << " s vs " << free.seconds
                              << " s without hold = " << holdSec * 1000.0 << " ms\n";
                    return 1;
                }
                holdReport += juce::String ((neural ? " neural/" : " yin/") + juce::String (holdBlock)
                                            + "=" + juce::String (holdSec * 1000.0, 0) + "ms").toStdString();
            }
        std::cout << "  targetHold=PASS" << holdReport << "\n";
    }

    // --- Held target dropped when the key or Edit-Notes mask changes ---------
    for (const bool changeMask : { false, true })
    {
        const double sec = retargetAfterRuleChangeSeconds (changeMask, 64);
        if (sec < 0.0 || sec > 0.030)
        {
            std::cerr << "held target outlived a " << (changeMask ? "note-mask" : "key")
                      << " change: " << sec << " s (E4 no longer allowed)\n";
            return 1;
        }
    }
    std::cout << "  heldTargetRuleChange=PASS (key + Edit Notes change retarget within one hop)\n";

    const double liveClassicB3ErrorCents = renderClassicStrongB3Plus45ErrorCents (true);
    if (liveClassicB3ErrorCents > 25.0)
    {
        std::cerr << "Production Live Mode Classic strong correction missed B3 +45c steady target; absErrorCents="
                  << liveClassicB3ErrorCents << "\n";
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
        auto runKey = [&] (const juce::AudioBuffer<float>& in, int manualRoot = -1, int manualScaleMode = -1)
        {
            SattariAutoPitchAudioProcessor p;
            auto& a = p.parameters();
            setParameter (a, "autoKey", 1.0f);
            if (manualRoot >= 0)      setParameter (a, "keyRoot", (float) manualRoot);
            if (manualScaleMode >= 0) setParameter (a, "scaleMode", (float) manualScaleMode);
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
            double melodyPhase = 0.0;
            const int noteLen = (int) (48000 * 0.26);
            for (int i = 0; i < tonal.getNumSamples(); ++i)
            {
                const int midi = 60 + deg[seq[(i / noteLen) % 20]];
                const double hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
                d[i] = 0.4f * (float) std::sin (melodyPhase);
                melodyPhase += 2.0 * juce::MathConstants<double>::pi * hz / 48000.0;
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
        //  Fallback rules below the gate (2026-09-24):
        //    confident                          -> detected key
        //    not confident + explicit manual key -> that manual key
        //    not confident + untouched default   -> Chromatic (A minor is not a decision)
        if (! (tonalKey.root == 0 && tonalKey.scale == 0))
        {
            std::cerr << "Confident AutoKey did not use the detected C major: "
                      << tonalKey.root << "/" << tonalKey.scale << "\n";
            return 1;
        }
        if (noiseKey.scale != 2)
        {
            std::cerr << "AutoKey No Result with an untouched manual key did not fall back to Chromatic: scale "
                      << noiseKey.scale << "\n";
            return 1;
        }
        const auto explicitAMinor = runKey (noise, 9, 1);    // same VALUE as the default, chosen on purpose
        if (explicitAMinor.root != 9 || explicitAMinor.scale != 1)
        {
            std::cerr << "AutoKey No Result ignored an explicitly chosen A minor: "
                      << explicitAMinor.root << "/" << explicitAMinor.scale << "\n";
            return 1;
        }
        const auto explicitDMajor = runKey (noise, 2, 0);
        if (explicitDMajor.root != 2 || explicitDMajor.scale != 0)
        {
            std::cerr << "AutoKey No Result ignored an explicitly chosen D major: "
                      << explicitDMajor.root << "/" << explicitDMajor.scale << "\n";
            return 1;
        }

        //  The explicit/untouched distinction is session state: it survives save and
        //  reload, a restore is not itself a choice, and pre-flag sessions keep their
        //  manual key as the fallback.
        {
            auto roundTrip = [] (SattariAutoPitchAudioProcessor& from, bool stripFlag)
            {
                juce::MemoryBlock mb; from.getStateInformation (mb);
                if (stripFlag)
                {
                    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
                    xml->removeAttribute ("manualKeyExplicit");
                    mb.reset(); juce::AudioProcessor::copyXmlToBinary (*xml, mb);
                }
                auto to = std::make_unique<SattariAutoPitchAudioProcessor>();
                to->setStateInformation (mb.getData(), (int) mb.getSize());
                return to->manualKeyIsExplicit();
            };
            SattariAutoPitchAudioProcessor untouched, chosen;
            setParameter (chosen.parameters(), "keyRoot", 4.0f);
            if (untouched.manualKeyIsExplicit() || ! chosen.manualKeyIsExplicit()
                || roundTrip (untouched, false) || ! roundTrip (chosen, false) || ! roundTrip (untouched, true))
            {
                std::cerr << "Manual-key intent did not persist correctly through save/restore\n";
                return 1;
            }
        }
        std::cout << "  autoKeyGate=tonal " << tonalKey.confidence
                  << " >= " << gate << " > noise " << noiseKey.confidence
                  << " (untouched -> Chromatic; explicit manual key kept; persists)\n";
    }

    // --- Session state + Humanize persistence (permanent release regressions) -----------
    //  Both defects were found by the 2026-09-25 no-harm matrix and fixed in cbd1ce4:
    //  (1) every save appended another KeyMap / PitchEdits / MidiLearn section and restore read
    //      the OLDEST copy, so the blob grew and deleted pitch notes came back;
    //  (2) Humanize drew from an unseeded RNG, so two bounces of one session differed.
    {
        using Xml = std::unique_ptr<juce::XmlElement>;
        auto saveXml = [] (SattariAutoPitchAudioProcessor& p)
        {
            juce::MemoryBlock mb; p.getStateInformation (mb);
            return Xml (juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()));
        };
        auto blobOf = [] (const juce::XmlElement& x) { juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (x, mb); return mb; };
        auto loadXml = [&blobOf] (SattariAutoPitchAudioProcessor& p, const juce::XmlElement& x)
        { const auto mb = blobOf (x); p.setStateInformation (mb.getData(), (int) mb.getSize()); };
        auto count = [] (const juce::XmlElement& x, const char* name)
        { int n = 0; for (auto* c : x.getChildIterator()) n += c->hasTagName (name); return n; };
        auto keyMapXml = [] (int segments)
        {
            auto* km = new juce::XmlElement ("KeyMap");
            km->setAttribute ("file", "song.wav"); km->setAttribute ("dur", 120.0);
            for (int i = 0; i < segments; ++i)
            { auto* sg = km->createNewChildElement ("Seg"); sg->setAttribute ("t", 30.0 * i); sg->setAttribute ("r", 2 + i);
              sg->setAttribute ("s", 0); sg->setAttribute ("c", 0.4); }
            return km;
        };
        auto editsXml = [] (std::initializer_list<int> ids)
        {
            auto* pe = new juce::XmlElement ("PitchEdits");
            for (int id : ids)
            { auto* n = pe->createNewChildElement ("Note"); n->setAttribute ("id", id); n->setAttribute ("start", 1.0 * id);
              n->setAttribute ("end", 1.0 * id + 0.5); n->setAttribute ("source", 60 + id); n->setAttribute ("target", 60 + id);
              n->setAttribute ("confidence", 0.9); n->setAttribute ("enabled", 1); }
            return pe;
        };
        auto midiXml = [] (int cc, const char* param)
        { auto* ml = new juce::XmlElement ("MidiLearn"); auto* m = ml->createNewChildElement ("Map");
          m->setAttribute ("cc", cc); m->setAttribute ("p", param); return ml; };
        auto midiCc = [] (const juce::XmlElement& x)
        { if (auto* ml = x.getChildByName ("MidiLearn")) if (auto* m = ml->getChildByName ("Map")) return m->getIntAttribute ("cc", -1); return -1; };
        auto ids = [] (SattariAutoPitchAudioProcessor& p)
        { std::vector<juce::int64> v; for (const auto& e : p.getPitchEdits()) v.push_back (e.id); return v; };
        auto fail = [] (const juce::String& why) { std::cerr << "State persistence: " << why << "\n"; return 1; };

        // Base session with one of each section.
        SattariAutoPitchAudioProcessor seed;
        auto base = saveXml (seed);
        for (const char* n : { "KeyMap", "PitchEdits", "MidiLearn" }) base->deleteAllChildElementsWithTagName (n);
        base->addChildElement (keyMapXml (2)); base->addChildElement (editsXml ({ 1, 2, 3 }));
        base->addChildElement (midiXml (7, "amount"));

        // 1. Five save/load cycles through fresh instances: stable size, one copy of each, content intact.
        juce::MemoryBlock first;
        {
            auto doc = std::make_unique<juce::XmlElement> (*base);
            for (int cycle = 0; cycle < 5; ++cycle)
            {
                SattariAutoPitchAudioProcessor p;
                loadXml (p, *doc);
                doc = saveXml (p);
                const auto mb = blobOf (*doc);
                if (cycle == 0) first = mb;
                else if (mb.getSize() != first.getSize())
                    return fail ("blob grew across save/load cycles: " + juce::String ((int) first.getSize()) + " -> " + juce::String ((int) mb.getSize()));
                if (count (*doc, "KeyMap") != 1 || count (*doc, "PitchEdits") != 1 || count (*doc, "MidiLearn") != 1)
                    return fail ("save wrote duplicate session sections");
                if (ids (p) != std::vector<juce::int64> { 1, 2, 3 } || p.getKeyMapSegments().size() != 2 || midiCc (*doc) != 7)
                    return fail ("pitch edits / key map / MIDI Learn changed across cycles");
            }
        }
        // 2. A deleted pitch note stays deleted through two save/load cycles.
        {
            SattariAutoPitchAudioProcessor a; loadXml (a, *base);
            if (! a.deletePitchEdit (2)) return fail ("could not delete pitch note 2");
            auto x = saveXml (a);
            SattariAutoPitchAudioProcessor b; loadXml (b, *x);
            auto y = saveXml (b);
            SattariAutoPitchAudioProcessor c; loadXml (c, *y);
            if (ids (c) != std::vector<juce::int64> { 1, 3 })
                return fail ("a deleted pitch note came back after save/load");
        }
        // 3. A document saved by the defective build (old sections first, newest last) repairs:
        //    the NEWEST copy wins and the next save is canonical (one copy each, then stable).
        {
            auto legacy = std::make_unique<juce::XmlElement> (*base);
            for (const char* n : { "KeyMap", "PitchEdits", "MidiLearn" }) legacy->deleteAllChildElementsWithTagName (n);
            legacy->addChildElement (keyMapXml (1)); legacy->addChildElement (editsXml ({ 1, 2, 3 }));
            legacy->addChildElement (midiXml (7, "amount"));
            legacy->addChildElement (keyMapXml (3)); legacy->addChildElement (editsXml ({ 1, 3 }));
            legacy->addChildElement (midiXml (11, "speed"));
            SattariAutoPitchAudioProcessor p; loadXml (p, *legacy);
            if (ids (p) != std::vector<juce::int64> { 1, 3 } || p.getKeyMapSegments().size() != 3)
                return fail ("duplicate-section session did not restore the newest copy");
            auto saved = saveXml (p);
            if (count (*saved, "KeyMap") != 1 || count (*saved, "PitchEdits") != 1 || count (*saved, "MidiLearn") != 1
                || midiCc (*saved) != 11)
                return fail ("repaired session did not save back in canonical form");
            SattariAutoPitchAudioProcessor q; loadXml (q, *saved);
            if (blobOf (*saveXml (q)) != blobOf (*saved))
                return fail ("repaired session is not stable on the next save/load");
        }

        // Humanize: a vocal-like tone with slow vibrato, Natural style, Humanize 0.4.
        auto renderHumanized = [] (SattariAutoPitchAudioProcessor& p)
        {
            setParameter (p.parameters(), "style", 0.0f);
            setParameter (p.parameters(), "humanize", 0.4f);
            setParameter (p.parameters(), "keyRoot", 0.0f);
            setParameter (p.parameters(), "scaleMode", 2.0f);
            p.prepareToPlay (48000.0, 512);
            std::vector<float> out;
            double phase = 0.0;
            for (int b = 0; b < 300; ++b)
            {
                juce::AudioBuffer<float> buf (2, 512);
                for (int i = 0; i < 512; ++i)
                {
                    const double t = (b * 512 + i) / 48000.0;
                    const double hz = midiToHz (57) * std::pow (2.0, (30.0 + 25.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t)) / 1200.0);
                    const float v = 0.18f * (float) std::sin (phase);
                    phase += juce::MathConstants<double>::twoPi * hz / 48000.0;
                    buf.setSample (0, i, v); buf.setSample (1, i, v);
                }
                juce::MidiBuffer m; p.processBlock (buf, m);
                for (int i = 0; i < 512; ++i) out.push_back (buf.getSample (0, i));
            }
            return out;
        };
        auto seedOf = [&saveXml] (SattariAutoPitchAudioProcessor& p) { return saveXml (p)->getStringAttribute ("humanizeSeed"); };
        {
            SattariAutoPitchAudioProcessor a;
            const auto first = renderHumanized (a);
            const auto again = renderHumanized (a);                 // same instance, bounced again
            auto st = saveXml (a);
            SattariAutoPitchAudioProcessor b; loadXml (b, *st);
            const auto reopened = renderHumanized (b);              // saved + reopened session
            if (again != first) return fail ("Humanize: bouncing the same session twice differs");
            if (reopened != first) return fail ("Humanize: save/reopen changes the bounce");
            SattariAutoPitchAudioProcessor c;                       // a second instance (doubled vocal)
            if (seedOf (c) == seedOf (a)) return fail ("Humanize: two instances got the same seed");
            if (renderHumanized (c) == first) return fail ("Humanize: two instances on a doubled vocal render identically");
            // A project saved before the seed existed loads, renders, and from its next save on is reproducible.
            auto legacy = saveXml (a); legacy->removeAttribute ("humanizeSeed");
            SattariAutoPitchAudioProcessor d; loadXml (d, *legacy);
            const auto legacyOut = renderHumanized (d);
            if (std::any_of (legacyOut.begin(), legacyOut.end(), [] (float v) { return ! std::isfinite (v); }))
                return fail ("Humanize: legacy project renders non-finite audio");
            auto resaved = saveXml (d);
            if (resaved->getStringAttribute ("humanizeSeed").isEmpty()) return fail ("Humanize: legacy project did not gain a seed on save");
            SattariAutoPitchAudioProcessor e1, e2; loadXml (e1, *resaved); loadXml (e2, *resaved);
            if (renderHumanized (e1) != renderHumanized (e2)) return fail ("Humanize: migrated legacy project is not reproducible");
        }
        std::cout << "  statePersistence=PASS (5 save/load cycles stable, deletes persist, legacy duplicates repair to "
                     "canonical; Humanize reproducible across re-bounce and reopen, distinct per instance, legacy migrates)\n";
    }

    // --- Dynamic Sattari Key -> Auto Pitch handoff -------------------------
    // Prove the actual realtime-safe SessionReader follows successive accepted
    // revisions and preserves the publisher's gated margin for the UI.
    {
        sattari::xplugin::SessionMessage first;
        first.root = 0;
        first.scale = 0;
        first.dynamicKey = true;
        first.keyConfidence = 0.31f;
        first.keyRevision = 1;
        first.keyWindowSeconds = 1.0;
        if (! sattari::xplugin::publishSessionState ("song-key", first))
        {
            std::cerr << "Dynamic Song Key first publish failed\n";
            return 1;
        }

        SattariAutoPitchAudioProcessor follower;
        setParameter (follower.parameters(), "followSongKey", 1.0f);
        follower.prepareToPlay (48000.0, 512);
        auto origin = follower.currentKeyOrigin();
        for (int i = 0; i < 30
             && origin.source != SattariAutoPitchAudioProcessor::KeyOrigin::Source::FollowBus;
             ++i)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
            origin = follower.currentKeyOrigin();
        }
        if (origin.source != SattariAutoPitchAudioProcessor::KeyOrigin::Source::FollowBus
            || origin.root != 0 || origin.scale != 0
            || std::abs (origin.confidence - 0.31f) > 1.0e-4f
            || ! origin.dynamic || origin.revision != 1
            || std::abs (origin.windowSeconds - 1.0f) > 1.0e-4f)
        {
            std::cerr << "Auto Pitch missed first Dynamic Key revision\n";
            return 1;
        }

        auto second = first;
        second.root = 7;
        second.scale = 1;
        second.keyConfidence = 0.18f;
        second.keyRevision = 2;
        if (! sattari::xplugin::publishSessionState ("song-key", second))
        {
            std::cerr << "Dynamic Song Key second publish failed\n";
            return 1;
        }
        for (int i = 0; i < 30 && origin.root != 7; ++i)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
            origin = follower.currentKeyOrigin();
        }
        const auto dynamicSummary = follower.getKeyDetectionTabSummary();
        follower.releaseResources();
        if (origin.source != SattariAutoPitchAudioProcessor::KeyOrigin::Source::FollowBus
            || origin.root != 7 || origin.scale != 1
            || std::abs (origin.confidence - 0.18f) > 1.0e-4f
            || ! origin.dynamic || origin.revision != 2
            || ! dynamicSummary.contains ("60% confidence")
            || ! dynamicSummary.contains ("Dynamic Song Key"))
        {
            std::cerr << "Auto Pitch missed changed Dynamic Key revision\n";
            return 1;
        }
        sattari::xplugin::keyBusFile().deleteFile();
        std::cout << "  dynamicKeyFollow=PASS (C major -> G minor, margins preserved)\n";
    }

    std::cout << "Auto Pitch core contract ok\n";
    std::cout << "  status=" << status << "\n";
    std::cout << "  keyTab=" << keyTabSummary << "\n";
    std::cout << "  changedEnergy=" << changedEnergy << "\n";
    std::cout << "  classicB3Plus45AbsErrorCents=" << classicB3ErrorCents
              << " (AutoKey No Result: untouched " << autoKeyUntouchedB3Cents
              << ", explicit " << autoKeyExplicitB3Cents << ")\n";
    std::cout << "  autoKeyStrengthParity=PASS (" << parityRenders << " renders, worst AutoKey-vs-twin "
              << parityWorstCents << " c; 3 voices x 2 strengths + Live x 3 key sources)\n";
    std::cout << "  vibratoNearBoundaryAbsErrorCents=" << vibratoBoundaryErrorCents
              << " (no target hold: " << vibratoBoundaryNoHoldCents << ")\n";
    std::cout << "  productionLiveClassicB3Plus45AbsErrorCents=" << liveClassicB3ErrorCents << "\n";
    std::cout << "  modes=Natural, Classic, Modern Rap, Robot, Big Harmony\n";
#if SATTARI_AUTOPITCH_PRINT_SIGNALSMITH && SATTARI_AUTOPITCH_HAS_SIGNALSMITH
    std::cout << "  engine=signalsmith\n";
#elif SATTARI_AUTOPITCH_HAS_RUBBERBAND
    std::cout << "  engine=rubberband\n";
#else
    std::cout << "  engine=fallback-guide\n";
#endif
    return 0;
}
