// Sattari Compass — functional contract (legacy Brain binary identity).
//
// Compass is the session conductor: it detects key + reference tuning, lets the user
// edit the rest of the SessionState, and publishes the FULL v2 SessionState to
// the cross-plugin bus while passing audio through untouched. This proves:
//   1. audio pass-through is bit-exact (Compass never touches the signal);
//   2. editing chord / time-sig / swing / section / energy / maqam + Publish
//      makes readSession() return exactly those v2 fields;
//   3. a manual key override publishes that key;
//   4. a reference override publishes that referenceHz;
//   5. nothing is NaN / non-finite.
//
// The publish path runs on Compass's ~700 ms worker thread (as it does in a DAW),
// so each edit is verified by polling readSession() until it reflects the edit
// (with a timeout), the same pattern the bus's SessionReader contract uses.
#include "BrainProcessor.h"
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariEditorRender.h>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <thread>
#include <functional>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Poll the bus until `pred(msg)` holds or we time out. Returns the last-read
// message (so callers can assert on it) and whether the predicate was met.
bool waitForSession (SattariBrainAudioProcessor& processor,
                     const std::function<bool (const sattari::xplugin::SessionMessage&)>& pred,
                     sattari::xplugin::SessionMessage& out, int timeoutMs = 6000)
{
    const int stepMs = 50;
    for (int waited = 0; waited <= timeoutMs; waited += stepMs)
    {
        if (auto s = sattari::xplugin::readSession())
        {
            out = *s;
            if (pred (out)) return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (stepMs));
    }
    const auto detected = processor.detected();
    auto& parameters = processor.parameters();
    std::fprintf (stderr,
        "Publication timeout: completedAttempts=%u lastResult=%s publish=%g manual=%g detectedValid=%d confidence=%g\n",
        processor.publicationAttemptCount(),
        sattari::xplugin::sessionPublishResultName (processor.lastPublicationResult()),
        (double) parameters.getRawParameterValue ("brainPublish")->load(),
        (double) parameters.getRawParameterValue ("brainManual")->load(),
        (int) detected.valid, detected.confidence);
    return false;
}
} // namespace


//  6. the conductor detects with the SongKeyEstimator, not the old chroma path.
//
//  Compass used to publish `detectKey()` (Aarden/HPCP), which measured 39.2% exact
//  on 1337 labelled tracks against the estimator's 57.6%. Since Compass OUTRANKS
//  Sattari Key on the single bus slot (priority 100 vs 80), the whole suite was
//  receiving the weaker answer whenever both plugins were loaded.
//
//  Audio is fed in real-time-sized chunks with the worker left to run on its own
//  ~700 ms cadence, rather than force-calling analyseNow(). Two reasons: the
//  worker-thread state (chroma, estimator, feed cursor) has a single owner and
//  calling it from here would race it, and the estimator's feed is sized for a
//  live stream — pushing 45 s in at once would lap the ring and be counted as
//  dropped, which is correct behaviour but not what a DAW does.
static int checkSongKeyDetector()
{
    const double sr = 48000.0;
    const int block = 512;

    SattariBrainAudioProcessor p;
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);

    const double hz[3] = { 261.63, 329.63, 392.00 };   // C E G
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double phase[3] = { 0.0, 0.0, 0.0 };

    //  ~48 s of audio in 2 s chunks, pausing so the worker keeps up. The estimator
    //  needs about 30 s before it will answer at all.
    const int chunks = 24;
    const int blocksPerChunk = (int) (sr * 2.0 / block);
    for (int c = 0; c < chunks; ++c)
    {
        for (int b = 0; b < blocksPerChunk; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                float m = 0.0f;
                for (int v = 0; v < 3; ++v)
                {
                    m += 0.22f * (float) std::sin (phase[v]);
                    phase[v] += 2.0 * juce::MathConstants<double>::pi * hz[v] / sr;
                }
                buf.setSample (0, i, m);
                buf.setSample (1, i, m);
            }
            p.processBlock (buf, midi);
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (250));
    }
    //  Let a final worker pass land.
    std::this_thread::sleep_for (std::chrono::milliseconds (1600));

    const auto d = p.detected();
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    std::printf ("  songKey: valid=%d key=%s %s conf=%.4f\n",
                 (int) d.valid, d.valid ? names[((d.root % 12) + 12) % 12] : "-",
                 d.minor ? "minor" : "major", d.confidence);
    if (! d.valid || ((d.root % 12) + 12) % 12 != 0 || d.minor)
    {
        std::printf ("  FAIL: expected C major from ~48 s of a C major triad\n");
        return 1;
    }
    return 0;
}

// Independent analysis evidence for restricted test environments. This mode
// never calls a bus reader or publisher, constructs an editor, or restores state.
// It does not clear or replace the full publication contract below.
static int checkAnalysisOnly()
{
    int fails = 0;
    auto check = [&fails] (bool ok, const char* what)
    {
        if (! ok) { std::fprintf (stderr, "FAIL analysis-only: %s\n", what); ++fails; }
    };
    SattariBrainAudioProcessor processor;
    auto& parameters = processor.parameters();
    setParam (parameters, "brainPublish", 0.0f);
    const auto* publish = parameters.getRawParameterValue ("brainPublish");
    if (publish == nullptr || publish->load() != 0.0f)
    {
        std::fprintf (stderr, "FAIL analysis-only: publication was not disabled before prepare\n");
        return 1;
    }

    constexpr double sr = 48000.0;
    constexpr int block = 512;
    processor.setRateAndBufferSizeDetails (sr, block);
    processor.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buffer (2, block), original (2, block);
    juce::MidiBuffer midi; // Empty: no learned MIDI action can enable publication.
    bool bitExact = true, allFinite = true;
    auto processAndCheck = [&]
    {
        original.makeCopyOf (buffer, true);
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            bitExact = bitExact && std::memcmp (buffer.getReadPointer (ch),
                original.getReadPointer (ch), sizeof (float) * block) == 0;
            for (int i = 0; i < block; ++i)
                allFinite = allFinite && std::isfinite (buffer.getSample (ch, i));
        }
    };
    auto waitForDetected = [&] (const std::function<bool (const SattariBrainAudioProcessor::Detected&)>& pred)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (3);
        do
        {
            if (pred (processor.detected())) return true;
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
        while (std::chrono::steady_clock::now() < deadline);
        return pred (processor.detected());
    };

    for (int pass = 0; pass < 8; ++pass)
    {
        buffer.clear();
        processAndCheck();
    }
    const auto silent = processor.detected();
    check (silent.energy == 0.0f && silent.transientId == 0,
           "initial silence manufactured energy or an onset");

    double phase[3] = {};
    const double hz[3] = { 261.625565, 329.627557, 391.995436 };
    for (int pass = 0; pass < 64; ++pass)
    {
        for (int i = 0; i < block; ++i)
        {
            float value = 0.0f;
            for (int voice = 0; voice < 3; ++voice)
            {
                value += 0.18f * (float) std::sin (phase[voice]);
                phase[voice] += juce::MathConstants<double>::twoPi * hz[voice] / sr;
            }
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }
        processAndCheck();
    }
    check (waitForDetected ([] (const SattariBrainAudioProcessor::Detected& d)
        { return d.energy > 0.1f && d.transientId > 0 && d.chordValid
                 && d.chordRoot == 0 && d.chordQuality == 1; }),
        "C-major chord, energy and onset were not detected in memory");
    const auto tone = processor.detected();
    check (std::isfinite (tone.confidence) && std::isfinite (tone.detuneCents)
               && std::isfinite (tone.energy) && tone.energy >= 0.0f && tone.energy <= 1.0f,
           "analysis produced invalid numeric readouts");

    for (int pass = 0; pass < 100; ++pass)
    {
        buffer.clear();
        processAndCheck();
    }
    check (waitForDetected ([tone] (const SattariBrainAudioProcessor::Detected& d)
        { return d.energy < tone.energy * 0.05f && d.transientId == tone.transientId; }),
        "silence did not decay energy or manufactured a new onset");
    processor.releaseResources(); // Join before inspecting final publication counters.
    check (bitExact && allFinite, "audio passthrough was not finite and bit exact");
    check (publish->load() == 0.0f && processor.publicationAttemptCount() == 0
               && processor.lastPublicationResult() == sattari::xplugin::SessionPublishResult::notAttempted
               && ! processor.detected().publishing,
           "disabled analysis mode attempted publication");
    std::printf ("Sattari Compass analysis-only %s (%d); publication NOT TESTED\n",
                 fails == 0 ? "ok" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 2 && juce::String (argv[1]) == "--analysis-only")
        return checkAnalysisOnly();

    if (argc == 3 && juce::String (argv[1]) == "--render-ui")
    {
        SattariBrainAudioProcessor processor;
        const juce::File destination (argv[2]);
        const bool ok = sattari::test::renderEditorToPng (processor, destination);
        std::printf ("%s %s\n", ok ? "rendered" : "FAIL render",
                     destination.getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }

    int fails = 0;
    auto check = [&fails] (bool ok, const char* what)
    {
        if (! ok) { std::fprintf (stderr, "FAIL: %s\n", what); ++fails; }
    };

    SattariBrainAudioProcessor proc;
    check (proc.publicationAttemptCount() == 0
               && proc.lastPublicationResult() == sattari::xplugin::SessionPublishResult::notAttempted,
           "constructor unexpectedly attempted publication");
    auto& a = proc.parameters();
    const double sr = 48000.0; const int block = 512;
    proc.prepareToPlay (sr, block);

    // 1) Bit-exact pass-through + finite. Feed a known stereo signal; Compass must
    //    return it untouched, sample for sample.
    bool allFinite = true, bitExact = true;
    {
        const int n = block * 8;
        juce::MidiBuffer midi;
        double ph = 0.0;
        for (int start = 0; start < n; start += block)
        {
            juce::AudioBuffer<float> buf (2, block);
            juce::AudioBuffer<float> copy (2, block);
            for (int i = 0; i < block; ++i)
            {
                const float l = 0.6f * (float) std::sin (ph);
                const float rr = 0.4f * (float) std::sin (ph * 1.5);
                buf.setSample (0, i, l);  copy.setSample (0, i, l);
                buf.setSample (1, i, rr); copy.setSample (1, i, rr);
                ph += 2.0 * M_PI * 220.0 / sr;
            }
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) allFinite = false;
                    const float expected = copy.getSample (ch, i);
                    if (std::memcmp (&v, &expected, sizeof (float)) != 0) bitExact = false;
                }
        }
    }
    check (bitExact, "audio pass-through not bit-exact");
    check (allFinite, "pass-through produced non-finite samples");

    // 2) Edit the SessionState fields; the published SessionState must carry them.
    //    Choice indices: chordRoot 10 -> A (pc 9); chordQuality 4 -> min7;
    //    timeSigDen 3 -> 8; section 4 -> chorus (id 4).
    // Use an explicit key while testing edited context. Automatic mode now
    // correctly stays silent until a confident key commits; context must never
    // be attached to an invented startup C major.
    setParam (a, "brainManual",      1.0f);
    setParam (a, "brainRefOverride", 0.0f);
    setParam (a, "brainPublish",     1.0f);
    setParam (a, "brainChordRoot",    10.0f);
    setParam (a, "brainChordQuality", 4.0f);
    setParam (a, "brainTimeSigNum",   6.0f);
    setParam (a, "brainTimeSigDen",   3.0f);
    setParam (a, "brainSwing",        0.6f);
    setParam (a, "brainSection",      4.0f);
    setParam (a, "brainEnergy",       0.8f);
    setParam (a, "brainEnergyAuto",   0.0f);
    setParam (a, "brainMaqam",        5.0f);
    {
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession (proc, [] (const sattari::xplugin::SessionMessage& s)
        {
            return s.chordRoot == 9 && s.chordQuality == 4
                && s.timeSigNum == 6 && s.timeSigDen == 8
                && std::abs (s.swing - 0.6f) < 1.0e-3f
                && s.sectionId == 4
                && std::abs (s.energy - 0.8f) < 1.0e-3f
                && s.maqamId == 5;
        }, m);
        check (ok, "edited SessionState fields were not published");
        check (m.chordRoot == 9 && m.chordQuality == 4, "chord round-trip wrong");
        check (m.timeSigNum == 6 && m.timeSigDen == 8, "time signature round-trip wrong");
        check (std::abs (m.swing - 0.6f) < 1.0e-3f, "swing round-trip wrong");
        check (m.sectionId == 4, "section round-trip wrong");
        check (std::abs (m.energy - 0.8f) < 1.0e-3f, "energy round-trip wrong");
        check (m.maqamId == 5, "maqam round-trip wrong");
        check (std::isfinite (m.referenceHz) && std::isfinite (m.swing) && std::isfinite (m.energy),
               "published SessionState has non-finite fields");
    }

    // 3) Automatic energy and real onset evidence. Silence does not manufacture
    // events; a clear impulse increments the monotonic transient id, and a
    // sustained signal produces non-zero measured energy.
    setParam (a, "brainEnergyAuto", 1.0f);
    juce::int64 priorTransient = 0;
    if (auto s = sattari::xplugin::readSession()) priorTransient = s->transientId;
    {
        juce::MidiBuffer midi;
        for (int pass = 0; pass < 32; ++pass)
        {
            juce::AudioBuffer<float> buf (2, block);
            for (int i = 0; i < block; ++i)
            {
                const float value = pass == 0 && i == 0
                    ? 0.9f
                    : 0.35f * (float) std::sin (2.0 * M_PI * 110.0 * (double) i / sr);
                buf.setSample (0, i, value);
                buf.setSample (1, i, value);
            }
            proc.processBlock (buf, midi);
        }
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession (proc, [priorTransient] (const sattari::xplugin::SessionMessage& s)
        {
            return s.transientId > priorTransient && s.energy > 0.1f;
        }, m);
        check (ok, "automatic energy/onset context was not published");
    }

    // 4) Automatic chord suggestion. With the chord root left at None, a stable
    //    C-major triad is published as C major; the UI remains an override when
    //    the user chooses an explicit root.
    setParam (a, "brainChordRoot",    0.0f);
    setParam (a, "brainChordQuality", 0.0f);
    {
        juce::MidiBuffer midi;
        double phaseC = 0.0, phaseE = 0.0, phaseG = 0.0;
        for (int pass = 0; pass < 16; ++pass)
        {
            juce::AudioBuffer<float> buf (2, block);
            for (int i = 0; i < block; ++i)
            {
                const float value = 0.18f * (float) (std::sin (phaseC)
                                                   + std::sin (phaseE)
                                                   + std::sin (phaseG));
                phaseC += 2.0 * M_PI * 261.625565 / sr;
                phaseE += 2.0 * M_PI * 329.627557 / sr;
                phaseG += 2.0 * M_PI * 391.995436 / sr;
                buf.setSample (0, i, value);
                buf.setSample (1, i, value);
            }
            proc.processBlock (buf, midi);
        }
        proc.requestRescan();
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession (proc, [] (const sattari::xplugin::SessionMessage& s)
        {
            return s.chordRoot == 0 && s.chordQuality == 1;
        }, m);
        check (ok, "automatic C-major chord suggestion was not published");
    }

    // 5) Manual key override wins over detection: publishes exactly that key.
    setParam (a, "brainManual",      1.0f);
    setParam (a, "brainManualRoot",  7.0f);   // G
    setParam (a, "brainManualScale", 1.0f);   // minor
    {
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession (proc, [] (const sattari::xplugin::SessionMessage& s)
        {
            return s.root == 7 && s.scale == 1;
        }, m);
        check (ok, "manual key override was not published");
    }

    // 6) Reference override: publishes exactly that referenceHz.
    setParam (a, "brainRefOverride", 1.0f);
    setParam (a, "brainReferenceHz", 432.0f);
    {
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession (proc, [] (const sattari::xplugin::SessionMessage& s)
        {
            return std::abs (s.referenceHz - 432.0) < 0.5;
        }, m);
        check (ok, "reference override was not published");
    }

    proc.releaseResources();

    if (fails != 0) { std::fprintf (stderr, "Sattari Compass contract FAILED (%d)\n", fails); return 1; }
    if (checkSongKeyDetector() != 0)
    {
        std::printf ("Sattari Compass contract FAILED (song key detector)\n");
        return 1;
    }

    std::printf ("Sattari Compass contract ok\n");
    return 0;
}
