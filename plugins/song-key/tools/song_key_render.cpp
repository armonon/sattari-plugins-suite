// Sattari Key — offline detection proof.
//
// Feeds a C major chord (C4+E4+G4) into the analyser, runs the chroma->key
// chain, and asserts it detects C. Proves the master-key scan actually resolves
// the key from a full-mix-style signal, not just compiles.
//
//   song_key_render          # detect + assert C (local proof)
//   song_key_render --smoke  # assert a valid key is detected (CI-safe)
//   song_key_render --file-smoke # choose/drop path: decode + full-song analysis
//   song_key_render --live-tempo-smoke # live 120 BPM onset/tempo contract
//   song_key_render --dynamic-follow-smoke # C->G bus update + silence hold
//   song_key_render --analyse-file <audio> # print the production file verdict
//   song_key_render --dynamic-file <audio> [1-5] # print accepted live-key timeline
//   song_key_render --corpus <dir> [N]   # score the REAL PLUGIN on a labelled corpus
//
// --corpus is the honest end-to-end measurement: it streams each track through
// SattariSongKeyAudioProcessor::processBlock exactly as a host would, then reads
// detected() and scores MIREX. SattariKeyDatasetEval scores the shared detector
// core; this scores what the plugin actually ships, including its ring buffers,
// worker cadence and shared Aarden/chroma decision.
#include "SongKeyProcessor.h"
#include "../../../core/tests/ArrangementClipFixture.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <sattari_audio_core/SattariEditorRender.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

bool renderUi (const juce::File& destination, bool minimumSize)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    SattariSongKeyAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);
    processor.releaseResources();
    setParam (processor.parameters(), "publish", 0.0f);

    const double frequencies[3] = { 261.63, 329.63, 392.00 };
    double phases[3] = { 0.0, 0.0, 0.0 };
    for (int block = 0;
         block < (int) (0.8 * sampleRate / blockSize);
         ++block)
    {
        juce::AudioBuffer<float> audio (2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            float value = 0.0f;
            for (int tone = 0; tone < 3; ++tone)
            {
                value += 0.25f * (float) std::sin (phases[tone]);
                phases[tone] +=
                    2.0 * juce::MathConstants<double>::pi
                    * frequencies[tone] / sampleRate;
            }
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, value);
        }
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
    }
    for (int pass = 0; pass < 20; ++pass)
        processor.analyseNow();

    return sattari::test::renderEditorToPng (
        processor, destination,
        minimumSize ? 720 : 0,
        minimumSize ? 600 : 0);
}

bool fileAnalysisSmoke()
{
    constexpr double sampleRate = 48000.0;
    constexpr int seconds = 12;
    juce::AudioBuffer<float> song (2, (int) sampleRate * seconds);
    const double frequencies[] = { 261.6256, 329.6276, 391.9954 };
    double phases[] = { 0.0, 0.0, 0.0 };
    for (int sample = 0; sample < song.getNumSamples(); ++sample)
    {
        const int beatSamples = (int) (sampleRate * 0.5); // 120 BPM
        const int beatPosition = sample % beatSamples;
        const double pulse = std::exp (-beatPosition / (sampleRate * 0.035));
        const double envelope = 0.18 + 0.82 * pulse;
        float value = 0.0f;
        for (int tone = 0; tone < 3; ++tone)
        {
            value += (float) (0.22 * envelope * std::sin (phases[tone]));
            phases[tone] += juce::MathConstants<double>::twoPi
                * frequencies[tone] / sampleRate;
        }
        song.setSample (0, sample, value);
        song.setSample (1, sample, value);
    }

    const auto file = juce::File::createTempFile (".wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (sampleRate)
                             .withNumChannels (2)
                             .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    if (! writer->writeFromAudioSampleBuffer (song, 0, song.getNumSamples()))
        return false;
    writer.reset();

    SattariSongKeyAudioProcessor processor;
    if (! processor.analyseFileAsync (file))
    {
        file.deleteFile();
        return false;
    }

    auto detected = processor.detected();
    for (int wait = 0; detected.analysing && wait < 1000; ++wait)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
        detected = processor.detected();
    }

    const bool passed = ! detected.analysing
        && detected.error.isEmpty()
        && detected.source == SattariSongKeyAudioProcessor::AnalysisSource::File
        && detected.valid
        && detected.root == 0
        && ! detected.minor
        && detected.alternativeValid
        && detected.tempoValid
        && std::abs (detected.bpm - 120.0) <= 2.0
        && detected.segments >= 1;

    processor.setSessionLocked (true);
    const auto locked = processor.detected();
    const bool lockedCorrectly = locked.lockRequested
        && locked.sessionLocked
        && locked.publishing
        && locked.lockedRoot == 0
        && ! locked.lockedMinor;
    processor.listenLive();
    const bool returnedLive = processor.detected().source
        == SattariSongKeyAudioProcessor::AnalysisSource::Live;
    file.deleteFile();
    return passed && lockedCorrectly && returnedLive;
}

bool liveTempoSmoke()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int seconds = 14;
    SattariSongKeyAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);
    processor.releaseResources();
    setParam (processor.parameters(), "publish", 0.0f);

    const double frequencies[] = { 261.6256, 329.6276, 391.9954 };
    double phases[] = { 0.0, 0.0, 0.0 };
    int absoluteSample = 0;
    int sinceAnalysis = 0;
    for (int block = 0; block < seconds * (int) sampleRate / blockSize; ++block)
    {
        juce::AudioBuffer<float> audio (2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample, ++absoluteSample)
        {
            const int beatSamples = (int) (sampleRate * 0.5);
            const int beatPosition = absoluteSample % beatSamples;
            const double pulse = std::exp (-beatPosition / (sampleRate * 0.035));
            const double envelope = 0.18 + 0.82 * pulse;
            float value = 0.0f;
            for (int tone = 0; tone < 3; ++tone)
            {
                value += (float) (0.22 * envelope * std::sin (phases[tone]));
                phases[tone] += juce::MathConstants<double>::twoPi
                    * frequencies[tone] / sampleRate;
            }
            audio.setSample (0, sample, value);
            audio.setSample (1, sample, value);
        }
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        sinceAnalysis += blockSize;
        if (sinceAnalysis >= 2048)
        {
            sinceAnalysis = 0;
            processor.analyseNow();
        }
    }
    processor.analyseNow();
    const auto detected = processor.detected();
    return detected.source == SattariSongKeyAudioProcessor::AnalysisSource::Live
        && detected.tempoValid
        && std::abs (detected.bpm - 120.0) <= 2.0;
}

bool liveContextSmoke()
{
    const auto session = "live-context-" + juce::Uuid().toString();
#if JUCE_WINDOWS
    _putenv_s ("SATTARI_SESSION_ID", session.toRawUTF8());
#else
    setenv ("SATTARI_SESSION_ID", session.toRawUTF8(), 1);
#endif
    constexpr double rate = 48000;
    constexpr int blockSize = 512;
    SattariSongKeyAudioProcessor processor;
    processor.prepareToPlay (rate, blockSize);
    processor.releaseResources();
    setParam (processor.parameters(), "publish", 1);
    double time = 0;
    int sinceAnalysis = 0;
    auto feed = [&] (double seconds, bool silent)
    {
        for (int block = 0; block < (int) std::ceil (seconds * rate / blockSize); ++block)
        {
            juce::AudioBuffer<float> audio (2, blockSize);
            for (int i = 0; i < blockSize; ++i, time += 1.0 / rate)
            {
                double value = 0;
                if (! silent)
                    for (double hz : { 261.6256, 329.6276, 391.9954 })
                        value += 0.2 * std::sin (juce::MathConstants<double>::twoPi * hz * time);
                audio.setSample (0, i, (float) value);
                audio.setSample (1, i, (float) value);
            }
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
            sinceAnalysis += blockSize;
            if (sinceAnalysis >= 2048) { processor.serviceLiveAnalysis(); sinceAnalysis = 0; }
        }
    };
    bool ok = true;
    feed (6.0, true);
    ok &= ! processor.detected().valid && processor.detected().liveAnalysedSeconds < 0.001;
    feed (14.0, false);
    const auto early = processor.detected();
    ok &= ! early.valid && early.gatheringSongContext && early.liveAnalysedSeconds < 15.0;
    for (int i = 0; i < 200; ++i) processor.serviceLiveAnalysis();
    ok &= std::abs (processor.detected().liveAnalysedSeconds - early.liveAnalysedSeconds) < 0.0001;
    processor.setSessionLocked (true);
    ok &= ! processor.detected().sessionLocked && ! sattari::xplugin::readSession();
    feed (2.0, false);
    const auto ready = processor.detected();
    const auto published = sattari::xplugin::readSession();
    ok &= ready.valid && ! ready.gatheringSongContext && ready.root == 0 && ! ready.minor
        && ready.sessionLocked && published && published->root == 0;
    // Increasing context does not leave a premature candidate enabled. Restoring
    // the session preserves the user-selected horizon.
    processor.unlockSession();
    setParam (processor.parameters(), "songContextSeconds", 30);
    ok &= ! processor.detected().valid && processor.detected().gatheringSongContext;
    juce::MemoryBlock state;
    processor.getStateInformation (state);
    SattariSongKeyAudioProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    ok &= std::abs (restored.detected().liveRequiredSeconds - 30.0) < 0.001;
    processor.requestRescan();
    processor.analyseNow();
    ok &= processor.detected().liveAnalysedSeconds < 0.001 && ! processor.detected().valid;
    std::printf ("Song context: early %.3f s valid=%d; ready %.3f s root=%d minor=%d; %s\n",
        early.liveAnalysedSeconds, early.valid, ready.liveAnalysedSeconds, ready.root, ready.minor, ok ? "PASS" : "FAIL");
    return ok;
}

bool arrangementClipSmoke()
{
    const auto sessionId = "song-key-arrangement-" + juce::String (sattari::xplugin::currentTimeMs());
   #if JUCE_WINDOWS
    _putenv_s ("SATTARI_SESSION_ID", sessionId.toRawUTF8());
   #else
    setenv ("SATTARI_SESSION_ID", sessionId.toRawUTF8(), 1);
   #endif
    auto probe = std::make_shared<sattari::test::ClipReaderProbe>();
    SattariSongKeyAudioProcessor processor;
    setParam (processor.parameters(), "publish", 0.0f);
    if (! processor.analyseClipAsync (sattari::test::makeArrangementClip (probe), "DAW chorus"))
        return false;
    auto waitForAnalysis = [&]
    {
        for (int i = 0; processor.detected().analysing && i < 1000; ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        return processor.detected();
    };
    const auto result = waitForAnalysis();
    const bool validClip = result.valid && ! result.analysing && result.error.isEmpty()
        && result.root == 0 && ! result.minor && result.tempoValid
        && std::abs (result.bpm - 120) < 2
        && result.source == SattariSongKeyAudioProcessor::AnalysisSource::Arrangement
        && result.sourceName == "DAW chorus" && sattari::test::readOnlyTrimmedClip (*probe);
    setParam (processor.parameters(), "publish", 1.0f);
    processor.setSessionLocked (true);
    const auto bus = sattari::xplugin::readSession();
    const bool publishedClip = processor.detected().sessionLocked && bus
        && bus->root == 0 && bus->scale == 0 && ! bus->dynamicKey
        && std::abs (bus->bpm - 120) < 2;
    setParam (processor.parameters(), "publish", 0.0f);
    processor.listenLive();
    if (! validClip || ! publishedClip || ! probe->destroyedOnOwnerThread.load()) return false;

    auto invalidated = std::make_shared<sattari::test::ClipReaderProbe>();
    invalidated->failAfterReads = 2;
    processor.analyseClipAsync (sattari::test::makeArrangementClip (invalidated), "Changed clip");
    const auto failed = waitForAnalysis();
    if (failed.valid || failed.analysing || failed.error.isEmpty()) return false;

    auto cancelled = std::make_shared<sattari::test::ClipReaderProbe>();
    cancelled->slow = true;
    processor.analyseClipAsync (sattari::test::makeArrangementClip (cancelled), "Cancelled clip");
    processor.listenLive();
    return cancelled->destroyedOnOwnerThread.load()
        && ! processor.detected().valid
        && processor.detected().source == SattariSongKeyAudioProcessor::AnalysisSource::Live;
}

bool dynamicFollowSmoke()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    // Isolate this proof from a running DAW and from parallel contract tests.
    const auto sessionId = "song-key-dynamic-" + juce::String (
        sattari::xplugin::currentTimeMs());
   #if JUCE_WINDOWS
    _putenv_s ("SATTARI_SESSION_ID", sessionId.toRawUTF8());
   #else
    setenv ("SATTARI_SESSION_ID", sessionId.toRawUTF8(), 1);
   #endif

    SattariSongKeyAudioProcessor processor;
    processor.prepareToPlay (sampleRate, blockSize);
    processor.releaseResources();
    setParam (processor.parameters(), "publish", 1.0f);
    setParam (processor.parameters(), "dynamicInterval", 3.0f);
    processor.setDynamicFollowEnabled (true);

    int sinceAnalysis = 0;
    auto feed = [&] (const std::array<double, 3>& frequencies,
                     double seconds, bool silence)
    {
        std::array<double, 3> phases {};
        const int blocks = (int) std::ceil (seconds * sampleRate / blockSize);
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> audio (2, blockSize);
            audio.clear();
            if (! silence)
            {
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    float value = 0.0f;
                    for (int tone = 0; tone < 3; ++tone)
                    {
                        value += 0.25f * (float) std::sin (phases[(size_t) tone]);
                        phases[(size_t) tone] += juce::MathConstants<double>::twoPi
                            * frequencies[(size_t) tone] / sampleRate;
                    }
                    audio.setSample (0, sample, value);
                    audio.setSample (1, sample, value);
                }
            }
            juce::MidiBuffer midi;
            processor.processBlock (audio, midi);
            sinceAnalysis += blockSize;
            if (sinceAnalysis >= 2048)
            {
                sinceAnalysis = 0;
                processor.serviceLiveAnalysis();
            }
        }
        processor.serviceLiveAnalysis();
    };

    feed ({ 261.6256, 329.6276, 391.9954 }, 3.3, false); // C major
    const auto c = processor.detected();
    const auto cBus = sattari::xplugin::readSession();
    const bool cAccepted = c.dynamicEnabled && c.dynamicActive
        && c.dynamicRoot == 0 && ! c.dynamicMinor
        && c.dynamicRevision == 1 && c.publishing
        && cBus && cBus->dynamicKey && cBus->root == 0 && cBus->scale == 0
        && cBus->keyRevision == 1
        && std::abs (cBus->keyWindowSeconds - 3.0) < 1.0e-6
        && cBus->keyConfidence >= sattari::dsp::kReleaseKeyConfidence;

    // One second of IV followed by one second of V is a progression, not two
    // modulations. Neither isolated chord may move the accepted key.
    feed ({ 174.6141, 220.0000, 261.6256 }, 1.05, false); // F major
    feed ({ 195.9977, 246.9417, 293.6648 }, 1.05, false); // G major
    const auto progression = processor.detected();
    const bool progressionHeld = progression.dynamicActive
        && progression.dynamicRoot == 0 && ! progression.dynamicMinor
        && progression.dynamicRevision == 1;

    // C -> G is a harmonically confusable fifth relation, so the adaptive
    // safety layer extends the selected 3-second minimum to five observations.
    feed ({ 195.9977, 246.9417, 293.6648 }, 5.3, false); // G major
    const auto g = processor.detected();
    const auto gBus = sattari::xplugin::readSession();
    const bool gAccepted = g.dynamicActive && g.dynamicRoot == 7
        && ! g.dynamicMinor && g.dynamicRevision >= 2 && g.publishing
        && gBus && gBus->dynamicKey && gBus->root == 7 && gBus->scale == 0
        && gBus->keyRevision == g.dynamicRevision;
    const int heldRevision = g.dynamicRevision;

    feed ({ 0.0, 0.0, 0.0 }, 1.4, true);
    const auto held = processor.detected();
    const auto heldBus = sattari::xplugin::readSession();
    const bool silenceHeld = held.dynamicActive && held.dynamicRoot == 7
        && ! held.dynamicMinor && held.dynamicRevision == heldRevision
        && heldBus && heldBus->root == 7
        && heldBus->keyRevision == heldRevision;

    sattari::xplugin::keyBusFile().deleteFile();
    if (! cAccepted || ! progressionHeld || ! gAccepted || ! silenceHeld)
    {
        std::fprintf (stderr,
            "Dynamic state C=%d/%d rev%d G=%d/%d rev%d held=%d/%d rev%d\n",
            c.dynamicRoot, c.dynamicMinor ? 1 : 0, c.dynamicRevision,
            g.dynamicRoot, g.dynamicMinor ? 1 : 0, g.dynamicRevision,
            held.dynamicRoot, held.dynamicMinor ? 1 : 0, held.dynamicRevision);
    }
    return cAccepted && progressionHeld && gAccepted && silenceHeld;
}

int dynamicFileTimeline (const juce::File& file, double horizon)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (
        formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->sampleRate <= 0.0)
    {
        std::fprintf (stderr, "FAIL: cannot decode %s\n",
                      file.getFullPathName().toRawUTF8());
        return 2;
    }

    horizon = juce::jlimit (1.0, 5.0, std::round (horizon));
    const auto sessionId = "song-key-file-timeline-" + juce::String (
        sattari::xplugin::currentTimeMs());
   #if JUCE_WINDOWS
    _putenv_s ("SATTARI_SESSION_ID", sessionId.toRawUTF8());
   #else
    setenv ("SATTARI_SESSION_ID", sessionId.toRawUTF8(), 1);
   #endif

    constexpr int blockSize = 512;
    constexpr int analysisHop = 2048;
    SattariSongKeyAudioProcessor processor;
    processor.prepareToPlay (reader->sampleRate, blockSize);
    processor.releaseResources(); // deterministic: drive the production cadence here
    setParam (processor.parameters(), "publish", 0.0f);
    setParam (processor.parameters(), "dynamicInterval", (float) horizon);
    processor.setDynamicFollowEnabled (true);

    juce::AudioBuffer<float> audio (2, blockSize);
    juce::int64 position = 0;
    int sinceAnalysis = 0;
    int lastRevision = 0;
    int acceptedEvents = 0;
    static const char* names[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    std::printf ("file=%s\thorizon=%.0fs\tduration=%.3fs\n",
                 file.getFileName().toRawUTF8(), horizon,
                 (double) reader->lengthInSamples / reader->sampleRate);
    std::printf ("event_sec\tkey\tmargin\tlane\trevision\n");

    while (position < reader->lengthInSamples)
    {
        const int count = (int) juce::jmin (
            (juce::int64) blockSize, reader->lengthInSamples - position);
        audio.clear();
        if (! reader->read (&audio, 0, count, position, true, true))
        {
            std::fprintf (stderr, "FAIL: decode stopped at %.3f s\n",
                          (double) position / reader->sampleRate);
            return 2;
        }
        juce::MidiBuffer midi;
        processor.processBlock (audio, midi);
        position += count;
        sinceAnalysis += count;

        if (sinceAnalysis >= analysisHop)
        {
            sinceAnalysis = 0;
            processor.serviceLiveAnalysis();
            const auto d = processor.detected();
            if (d.dynamicActive && d.dynamicRevision != lastRevision)
            {
                std::printf ("%.3f\t%s %s\t%.4f\t%s\t%d\n",
                             (double) position / reader->sampleRate,
                             names[juce::jlimit (0, 11, d.dynamicRoot)],
                             d.dynamicMinor ? "minor" : "major",
                             d.dynamicConfidence,
                             d.dynamicFastLane ? "fast" : "stable",
                             d.dynamicRevision);
                lastRevision = d.dynamicRevision;
                ++acceptedEvents;
            }
        }
    }
    if (sinceAnalysis > 0)
        processor.serviceLiveAnalysis();

    const auto final = processor.detected();
    std::printf ("summary\taccepted_events=%d\tfinal=%s",
                 acceptedEvents,
                 final.dynamicActive
                    ? names[juce::jlimit (0, 11, final.dynamicRoot)] : "none");
    if (final.dynamicActive)
        std::printf (" %s\tmargin=%.4f\trevision=%d",
                     final.dynamicMinor ? "minor" : "major",
                     final.dynamicConfidence, final.dynamicRevision);
    if (final.dynamicPending)
        std::printf ("\tpending=%s %s %d/%d",
                     names[juce::jlimit (0, 11, final.dynamicPendingRoot)],
                     final.dynamicPendingMinor ? "minor" : "major",
                     final.dynamicPendingConfirmations,
                     final.dynamicRequiredConfirmations);
    std::printf ("\n");
    return final.dynamicActive ? 0 : 1;
}
}

namespace
{
// "C minor" / "Eb major" / "F# minor" -> root 0..11 + minor flag. Returns false on
// anything unparseable so a malformed label is skipped rather than scored as wrong.
bool parseKeyLabel (std::string s, int& root, bool& minor)
{
    while (! s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.erase (s.begin());
    while (! s.empty() && (s.back()  == ' ' || s.back()  == '\t' || s.back()  == '\r')) s.pop_back();
    if (s.size() < 2) return false;
    static const char* N[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    int pc = -1;
    switch (s[0]) { case 'C': pc=0; break; case 'D': pc=2; break; case 'E': pc=4; break;
                    case 'F': pc=5; break; case 'G': pc=7; break; case 'A': pc=9; break;
                    case 'B': pc=11; break; default: return false; }
    std::size_t i = 1;
    if (i < s.size() && (s[i] == '#' || s[i] == 's')) { pc = (pc + 1) % 12; ++i; }
    else if (i < s.size() && (s[i] == 'b' || s[i] == 'f')) { pc = (pc + 11) % 12; ++i; }
    std::string mode = s.substr (i);
    std::transform (mode.begin(), mode.end(), mode.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    minor = (mode.find ("min") != std::string::npos) || (mode.find (" m") != std::string::npos);
    root = pc;
    (void) N;
    return true;
}

// Standard MIREX weighted score for one prediction.
double mirexScore (int pRoot, bool pMin, int tRoot, bool tMin)
{
    if (pRoot == tRoot && pMin == tMin) return 1.0;                       // exact
    if (! pMin && ! tMin && (pRoot - tRoot + 12) % 12 == 7) return 0.5;   // dominant (major)
    if (pMin && tMin && (pRoot - tRoot + 12) % 12 == 7) return 0.5;
    if (! tMin && pMin && (pRoot - tRoot + 12) % 12 == 9) return 0.3;     // relative minor
    if (tMin && ! pMin && (pRoot - tRoot + 12) % 12 == 3) return 0.3;     // relative major
    if (pRoot == tRoot && pMin != tMin) return 0.2;                       // parallel
    return 0.0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc == 2 && std::strcmp (argv[1], "--context-smoke") == 0)
    {
        juce::ScopedJuceInitialiser_GUI gui;
        return liveContextSmoke() ? 0 : 1;
    }
    if (argc == 2 && std::strcmp (argv[1], "--clip-smoke") == 0)
    {
        juce::ScopedJuceInitialiser_GUI gui;
        const bool ok = arrangementClipSmoke();
        std::printf ("Sattari Key arrangement reader: %s (trim, key/tempo, invalidation, cancellation, ownership)\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    if ((argc == 3 || argc == 4)
        && std::strcmp (argv[1], "--dynamic-file") == 0)
    {
        const double horizon = argc == 4 ? std::atof (argv[3]) : 3.0;
        return dynamicFileTimeline (
            juce::File (juce::String::fromUTF8 (argv[2])), horizon);
    }

    if (argc == 3 && std::strcmp (argv[1], "--analyse-file") == 0)
    {
        SattariSongKeyAudioProcessor processor;
        setParam (processor.parameters(), "publish", 0.0f);
        if (! processor.analyseFileAsync (juce::File (argv[2])))
        {
            std::fprintf (stderr, "FAIL: file was not accepted\n");
            return 1;
        }
        auto detected = processor.detected();
        while (detected.analysing)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
            detected = processor.detected();
        }
        if (! detected.valid)
        {
            std::fprintf (stderr, "FAIL: %s\n", detected.error.toRawUTF8());
            return 1;
        }
        static const char* names[] = {
            "C", "C#", "D", "D#", "E", "F",
            "F#", "G", "G#", "A", "A#", "B"
        };
        std::printf ("%s %s | margin %.3f",
                     names[detected.root % 12], detected.minor ? "minor" : "major",
                     detected.confidence);
        if (detected.alternativeValid)
            std::printf (" | alternate %s %s",
                         names[detected.alternativeRoot % 12],
                         detected.alternativeMinor ? "minor" : "major");
        if (detected.tempoValid)
            std::printf (" | %.2f BPM (tempo confidence %.3f)%s",
                         detected.bpm, detected.tempoConfidence,
                         detected.tempoOctaveAmbiguous ? " [half/double ambiguous]" : "");
        std::printf (" | tuning %+.1f cents | sections %d | agreement %.1f%%%s\n",
                     detected.detuneCents, detected.segments,
                     100.0 * detected.segmentAgreement,
                     detected.possibleModulation ? " | possible modulation" : "");
        return 0;
    }

    if (argc == 2 && std::strcmp (argv[1], "--file-smoke") == 0)
    {
        if (! fileAnalysisSmoke())
        {
            std::fprintf (stderr, "FAIL: full-song file analysis workflow\n");
            return 1;
        }
        std::printf ("Sattari Key file analysis smoke ok\n");
        return 0;
    }

    if (argc == 2 && std::strcmp (argv[1], "--live-tempo-smoke") == 0)
    {
        if (! liveTempoSmoke())
        {
            std::fprintf (stderr, "FAIL: live tempo workflow\n");
            return 1;
        }
        std::printf ("Sattari Key live tempo smoke ok\n");
        return 0;
    }

    if (argc == 2 && std::strcmp (argv[1], "--dynamic-follow-smoke") == 0)
    {
        if (! dynamicFollowSmoke())
        {
            std::fprintf (stderr, "FAIL: dynamic key follow workflow\n");
            return 1;
        }
        std::printf ("Sattari Key dynamic follow smoke ok\n");
        return 0;
    }

    if (argc == 3
        && (std::strcmp (argv[1], "--render-ui") == 0
            || std::strcmp (argv[1], "--render-ui-min") == 0))
    {
        juce::ScopedJuceInitialiser_GUI juceInitialiser;
        const bool minimumSize =
            std::strcmp (argv[1], "--render-ui-min") == 0;
        return renderUi (juce::File (argv[2]), minimumSize) ? 0 : 1;
    }

    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);

    // --corpus <dir> [N] : stream real labelled tracks through the actual plugin.
    if (argc > 2 && std::strcmp (argv[1], "--corpus") == 0)
    {
        const juce::File dir (juce::String::fromUTF8 (argv[2]));
        const int limit = (argc > 3) ? std::atoi (argv[3]) : 0;   // 0 = all
        std::ifstream labels (dir.getChildFile ("labels.csv").getFullPathName().toStdString());
        if (! labels) { std::fprintf (stderr, "FAIL: no labels.csv in %s\n", argv[2]); return 2; }

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        const int block = 512;
        int n = 0, exact = 0; double mirex = 0.0;
        std::string line;
        while (std::getline (labels, line))
        {
            if (limit > 0 && n >= limit) break;
            const auto comma = line.find (',');
            if (comma == std::string::npos) continue;
            std::string fname = line.substr (0, comma);
            fname.erase (std::remove (fname.begin(), fname.end(), '\r'), fname.end());
            int tRoot = 0; bool tMin = false;
            if (! parseKeyLabel (line.substr (comma + 1), tRoot, tMin)) continue;

            const auto wav = dir.getChildFile ("wav").getChildFile (juce::String::fromUTF8 (fname.c_str()));
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (wav));
            if (rd == nullptr) continue;

            const double sr = rd->sampleRate;
            SattariSongKeyAudioProcessor proc;
            proc.prepareToPlay (sr, block);
            proc.releaseResources();                          // drive analysis ourselves, no worker race
            setParam (proc.parameters(), "publish", 0.0f);    // never touch the live suite bus

            juce::AudioBuffer<float> buf (2, block);
            const juce::int64 total = rd->lengthInSamples;
            // Drive analyseNow() at the SAME 2048-sample cadence as the plugin's
            // worker, not at the historical 700 ms publisher cadence,
            // not once per block: the chroma accumulates per call, so a faster cadence
            // would measure something the shipping plugin never does.
            constexpr juce::int64 analysePeriod = 2048;
            juce::int64 sinceAnalyse = 0;
            for (juce::int64 pos = 0; pos + block <= total; pos += block)
            {
                buf.clear();
                rd->read (&buf, 0, block, pos, true, true);
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
                sinceAnalyse += block;
                if (sinceAnalyse >= analysePeriod) { sinceAnalyse = 0; proc.analyseNow(); }
            }
            proc.analyseNow();                                 // final settle

            const auto d = proc.detected();
            if (! d.valid) continue;
            const double s = mirexScore (d.root % 12, d.minor, tRoot, tMin);
            mirex += s; if (s >= 1.0) ++exact; ++n;
            if (n % 25 == 0) std::printf ("  ... %d tracks, running MIREX %.2f, exact %.1f%%\n",
                                          n, 100.0 * mirex / n, 100.0 * exact / n);
        }
        if (n == 0) { std::fprintf (stderr, "FAIL: no scorable tracks\n"); return 2; }
        std::printf ("\n== Sattari Key PLUGIN end-to-end on %d tracks ==\n", n);
        std::printf ("   MIREXx100 = %.2f     exact = %.1f%%  (%d/%d)\n",
                     100.0 * mirex / n, 100.0 * exact / n, exact, n);
        return 0;
    }

    const double sr = 48000.0;
    const int    block = 512;

    SattariSongKeyAudioProcessor proc;
    proc.prepareToPlay (sr, block);
    proc.releaseResources();                   // stop the background worker; we drive analysis directly
    setParam (proc.parameters(), "publish", 0.0f);   // don't touch the suite bus during the test

    // A real 16-second context, not repeated snapshots of the same short chord.
    const double f[3] = { 261.63, 329.63, 392.00 };
    double ph[3] = { 0, 0, 0 };
    const int blocks = (int) (16.0 * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int i = 0; i < block; ++i)
        {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) { s += 0.3f * (float) std::sin (ph[k]); ph[k] += 2.0 * juce::MathConstants<double>::pi * f[k] / sr; }
            buf.setSample (0, i, s); buf.setSample (1, i, s);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        if (b % 4 == 3) proc.analyseNow();
    }
    proc.analyseNow();

    const auto d = proc.detected();
    if (! d.valid) { std::fprintf (stderr, "FAIL: no key detected\n"); return 1; }
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    std::printf ("Sattari Key detected: %s %s (confidence %.3f)\n", names[d.root % 12], d.minor ? "minor" : "major", d.confidence);

    if (smoke) { std::printf ("Sattari Key render smoke ok\n"); return 0; }

    if (d.root != 0)
    { std::fprintf (stderr, "FAIL: C major chord detected as root %d, expected 0 (C)\n", d.root); return 1; }

    std::printf ("Sattari Key detection proof ok\n");
    return 0;
}
