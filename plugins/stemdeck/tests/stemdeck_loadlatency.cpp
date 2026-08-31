// A3: time-to-playable, measured — not asserted.
//
// loadDeckStemFileAsync comments "Deck is playable now; refine the grid/key in the
// background (§A3)". The milestone brief puts a <200ms bound on that. Nobody had
// measured it, and the cost is length-dependent (decode + first-pass analysis both
// scale with track duration), so a short synthetic clip proves nothing.
//
// This walks real full-length durations and reports:
//   t_playable  — load call -> deck has full-mix audio and is seekable
//   t_refined   — load call -> background neural refinement lands (may never)
//
//   StemDeckLoadLatency [seconds...]      default: 30 120 300
//
// It PRINTS numbers and only fails the <200ms bound, so a regression in refinement
// timing shows up as data rather than a red build.
#include "MainPluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>

namespace
{
juce::File writeTestTrack (double seconds, int sampleRate)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("stemdeck-a3-" + juce::String ((int) seconds) + "s.wav");
    if (f.existsAsFile() && f.getSize() > 1000)
        return f;   // reuse across runs; writing 5 minutes of PCM is the slow part

    const int n = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> buf (2, n);

    // Something with real beat structure so analysis has actual work to do: a 120 BPM
    // kick-ish transient train plus a sustained harmonic bed. Deterministic — no
    // Random — so repeat runs are comparable.
    const double beatSamples = 60.0 / 120.0 * sampleRate;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sampleRate;
        const double phase = std::fmod ((double) i, beatSamples) / beatSamples;
        const float transient = (float) (std::exp (-phase * 18.0) * std::sin (t * 55.0 * juce::MathConstants<double>::twoPi));
        const float bed = (float) (0.18 * std::sin (t * 220.0 * juce::MathConstants<double>::twoPi)
                                 + 0.12 * std::sin (t * 330.0 * juce::MathConstants<double>::twoPi));
        const float s = juce::jlimit (-1.0f, 1.0f, 0.6f * transient + bed);
        buf.setSample (0, i, s);
        buf.setSample (1, i, s * 0.95f);
    }

    f.deleteFile();
    juce::WavAudioFormat wav;
    if (std::unique_ptr<juce::OutputStream> os = f.createOutputStream())
    {
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (16);
        if (auto w = wav.createWriterFor (os, options))
            w->writeFromAudioSampleBuffer (buf, 0, n);
    }
    return f;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // Field-diagnostic mode: replay the exact processor state embedded in a recovery
    // project and verify the complete channel -> master path, not just DeckPlayer.
    if (argc == 3 && juce::String (argv[1]) == "--state-audio")
    {
        const juce::File project (argv[2]);
        const auto projectXml = juce::XmlDocument::parse (project);
        juce::MemoryBlock state;
        if (projectXml == nullptr
            || ! state.fromBase64Encoding (projectXml->getStringAttribute ("processorState")))
        {
            std::cerr << "Could not decode project processor state\n";
            return 2;
        }

        MainPluginProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setStateInformation (state.getData(), (int) state.getSize());
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 10000.0;
        while (proc.deckAt (0).getLoadedAudio() == nullptr
               && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

        if (auto* play = proc.parameters().getParameter ("deckAPlay"))
            play->setValueNotifyingHost (1.0f);
        juce::AudioBuffer<float> audio (2, 512);
        juce::MidiBuffer midi;
        float peak = 0.0f;
        for (int block = 0; block < 400; ++block)
        {
            audio.clear();
            proc.processBlock (audio, midi);
            peak = juce::jmax (peak, audio.getMagnitude (0, audio.getNumSamples()));
        }
        juce::AudioBuffer<float> direct (2, 512);
        direct.clear();
        std::array<float, DeckPlayer::numStemLanes> unity;
        unity.fill (1.0f);
        std::array<float, DeckPlayer::numStemLanes> noPitch {};
        proc.deckAt (0).process (direct, 0, direct.getNumSamples(), true,
                                 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                 TimePitchDecision {}, unity, noPitch,
                                 BeatSyncEngine::PlaybackPlan {});
        const float directPeak = direct.getMagnitude (0, direct.getNumSamples());
        const auto* playValue = proc.parameters().getRawParameterValue ("deckAPlay");
        const auto value = [&proc] (const char* id)
        {
            if (const auto* raw = proc.parameters().getRawParameterValue (id))
                return raw->load();
            return -999.0f;
        };
        std::cout << "state loaded=" << (proc.deckAt (0).getLoadedAudio() != nullptr)
                  << " duration=" << proc.getDeckTotalSeconds (0)
                  << " play=" << (playValue != nullptr ? playValue->load() : -1.0f)
                  << " playhead=" << proc.getDeckPlayheadSeconds (0)
                  << " type=" << (int) proc.getChannelType (0)
                  << " muted=" << proc.isChannelMuted (0)
                  << " soloed=" << proc.isChannelSoloed (0)
                  << " deck peak=" << proc.getDeckPeak (0, 0)
                  << " master peak=" << proc.getMasterPeak (0)
                  << " output peak=" << peak
                  << " vol=" << value ("deckAVolume")
                  << " fader=" << value ("deckAFader")
                  << " filter=" << value ("deckAFilter")
                  << " full=" << value ("deckAStemFullGain")
                  << " drums=" << value ("deckAStemDrumsGain")
                  << " master=" << value ("masterGain")
                  << " masterDrums=" << value ("masterStemDrumsGain")
                  << " safe=" << value ("safePerformance") << '\n';
        std::cout << "direct=" << directPeak
                  << " keyLock=" << value ("deckAKeyLock")
                  << " sync=" << value ("deckASync")
                  << " hostSync=" << value ("hostSync")
                  << " beatSync=" << value ("beatSync")
                  << " pitch=" << value ("deckAPitch")
                  << " low=" << value ("deckALow")
                  << " mid=" << value ("deckAMid")
                  << " high=" << value ("deckAHigh")
                  << " stemFilt=" << value ("deckAStemDrumsFilt")
                  << " stemSend=" << value ("deckAStemDrumsSend")
                  << " master=" << value ("master") << '\n';
        return peak > 1.0e-4f ? 0 : 1;
    }

    std::vector<double> lengths;
    for (int i = 1; i < argc; ++i) lengths.push_back (juce::String (argv[i]).getDoubleValue());
    if (lengths.empty()) lengths = { 30.0, 120.0, 300.0 };

    constexpr int sr = 44100;
    constexpr double kBoundMs = 200.0;

    std::cout << "A3 time-to-playable (bound: " << kBoundMs << " ms)" << std::endl;
    std::cout << std::fixed << std::setprecision (1);

    int over = 0;
    for (double secs : lengths)
    {
        const auto file = writeTestTrack (secs, sr);
        if (! file.existsAsFile()) { std::cout << "  could not write " << secs << "s track" << std::endl; continue; }

        MainPluginProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        const double t0 = juce::Time::getMillisecondCounterHiRes();
        proc.loadDeckFileAsync (0, file);

        double tPlayable = -1.0, tRefined = -1.0;
        const double deadline = t0 + 30000.0;   // generous; we are measuring, not gating throughput
        while (juce::Time::getMillisecondCounterHiRes() < deadline)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);

            if (tPlayable < 0.0)
                if (auto a = proc.deckAt (0).getStemAudio (DeckPlayer::fullMixStem))
                    if (a->audio.getNumSamples() > 0)
                        tPlayable = juce::Time::getMillisecondCounterHiRes() - t0;

            if (tPlayable >= 0.0 && tRefined < 0.0)
                if (proc.deckAt (0).getRefinedAnalysis() != nullptr)
                    tRefined = juce::Time::getMillisecondCounterHiRes() - t0;

            if (tPlayable >= 0.0 && tRefined >= 0.0) break;
        }

        const bool ok = tPlayable >= 0.0 && tPlayable < kBoundMs;
        if (! ok) ++over;

        bool streamBounded = true;
        bool streamAudible = true;
        if (secs > 60.0)
        {
            const auto loaded = proc.deckAt (0).getLoadedAudio();
            const auto fullPcmBytes = (std::size_t) 2 * (std::size_t) (secs * sr) * sizeof (float);
            streamBounded = loaded != nullptr && loaded->streamed != nullptr
                         && loaded->audio.getNumSamples() <= 1
                         && loaded->streamed->residentBytes() < fullPcmBytes;

            juce::AudioBuffer<float> output (2, 512);
            std::array<float, DeckPlayer::numStemLanes> gains;
            std::array<float, DeckPlayer::numStemLanes> pitches;
            gains.fill (1.0f);
            pitches.fill (0.0f);
            float peak = 0.0f;
            proc.deckAt (0).seekStart();
            for (int block = 0; block < 8; ++block)
            {
                output.clear();
                proc.deckAt (0).process (output, 0, output.getNumSamples(), true,
                                         0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         1.0f, TimePitchDecision {}, gains, pitches,
                                         BeatSyncEngine::PlaybackPlan {});
                peak = juce::jmax (peak, output.getMagnitude (0, output.getNumSamples()));
            }
            streamAudible = peak > 1.0e-4f;
            if (! streamBounded || ! streamAudible) ++over;
        }

        std::cout << "  " << std::setw (5) << secs << "s track   "
                  << "playable " << (tPlayable < 0.0 ? juce::String ("NEVER") : juce::String (tPlayable, 1) + " ms")
                  << (ok ? "  [ok]" : "  [OVER BOUND]")
                  << "   refined "
                  << (tRefined < 0.0 ? juce::String ("not within 30s") : juce::String (tRefined, 1) + " ms")
                  << (secs > 60.0
                          ? juce::String ("   stream ")
                                + (streamBounded ? "[bounded]" : "[UNBOUNDED]")
                                + (streamAudible ? "[audible]" : "[SILENT]")
                          : juce::String())
                  << std::endl;
    }

    // Rapid replacement regression: a slower A request used to complete after B
    // and silently put A back on the deck. Clear had the same resurrection bug.
    {
        const auto slow = writeTestTrack (10.0, sr);
        const auto fast = writeTestTrack (1.0, sr);
        MainPluginProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        // Replacing a separated source must not inherit its intentionally-muted
        // full-mix lane. That state produced a moving playhead with silent output
        // whenever the next song had no cached stems.
        if (auto* fullGain = proc.parameters().getParameter ("deckAStemFullGain"))
            fullGain->setValueNotifyingHost (fullGain->convertTo0to1 (0.0f));
        proc.loadDeckFileAsync (0, fast);
        const bool replacementAudible = std::abs (
            proc.parameters().getRawParameterValue ("deckAStemFullGain")->load()
                - stemdeck::stemgain::kUnity) < 0.01f;
        if (! replacementAudible) ++over;
        std::cout << "  replacement mix   "
                  << (replacementAudible ? "[full mix audible]" : "[FULL MIX MUTED]")
                  << std::endl;
        proc.clearDeck (0);

        proc.loadDeckStemFileAsync (0, 1, fast);
        const auto stemDeadline = juce::Time::getMillisecondCounterHiRes() + 1000.0;
        while (proc.deckAt (0).getStemAudio (1) == nullptr
               && juce::Time::getMillisecondCounterHiRes() < stemDeadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        const bool stemLoaded = proc.deckAt (0).getStemAudio (1) != nullptr;
        if (! stemLoaded) ++over;
        proc.loadDeckFileAsync (0, slow);
        const bool oldStemsDetached = stemLoaded && proc.deckAt (0).getStemAudio (1) == nullptr;
        if (! oldStemsDetached) ++over;
        std::cout << "  source replacement "
                  << (oldStemsDetached ? "[old stems detached]" : "[OLD STEM LEAKED]")
                  << std::endl;

        proc.loadDeckFileAsync (0, slow);
        proc.loadDeckFileAsync (0, fast);

        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 4000.0;
        while (juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);

        const bool newestWon = proc.getDeckPath (0) == fast.getFullPathName();
        if (! newestWon) ++over;
        std::cout << "  rapid A->B load   " << (newestWon ? "[newest wins]" : "[STALE A WON]")
                  << std::endl;

        proc.loadDeckFileAsync (0, slow);
        proc.clearDeck (0);
        const auto clearDeadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
        while (juce::Time::getMillisecondCounterHiRes() < clearDeadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        const bool stayedClear = proc.getDeckPath (0).isEmpty()
                              && proc.deckAt (0).getLoadedAudio() == nullptr;
        if (! stayedClear) ++over;
        std::cout << "  load->clear race  " << (stayedClear ? "[stays clear]" : "[STALE LOAD RESURRECTED]")
                  << std::endl;
    }

    std::cout << (over == 0 ? "A3_PLAYABLE_OK" : "A3_PLAYABLE_OVER_BOUND") << std::endl;

    return over == 0 ? 0 : 1;
}
