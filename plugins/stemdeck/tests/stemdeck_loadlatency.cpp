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
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sampleRate, 2, 16, {}, 0)))
        { os.release(); w->writeFromAudioSampleBuffer (buf, 0, n); }
    return f;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

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

        std::cout << "  " << std::setw (5) << secs << "s track   "
                  << "playable " << (tPlayable < 0.0 ? juce::String ("NEVER") : juce::String (tPlayable, 1) + " ms")
                  << (ok ? "  [ok]" : "  [OVER BOUND]")
                  << "   refined "
                  << (tRefined < 0.0 ? juce::String ("not within 30s") : juce::String (tRefined, 1) + " ms")
                  << std::endl;
    }

    std::cout << (over == 0 ? "A3_PLAYABLE_OK" : "A3_PLAYABLE_OVER_BOUND") << std::endl;

    // Tracktion keeps background threads that do not join on a plain return; the
    // engine force-kills them and buffered stdout dies with the process. Flush and
    // leave immediately, exactly as StemDeckEditorRender does.
    std::cout.flush();
    std::_Exit (over == 0 ? 0 : 1);
}
