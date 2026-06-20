#include "DeckPlayer.h"
#include "BeatSyncEngine.h"
#include "TimePitchEngine.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace
{
constexpr double sampleRate = 44100.0;

[[noreturn]] void fail (const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit (1);
}

std::shared_ptr<const DeckPlayer::LoadedAudio> makeSineAudio (double frequencyHz, double seconds)
{
    auto loaded = std::make_shared<DeckPlayer::LoadedAudio>();
    const int samples = (int) std::round (sampleRate * seconds);
    loaded->audio.setSize (2, samples);
    loaded->sourceSampleRate = sampleRate;
    loaded->path = "synthetic://440hz-contract-tone.wav";
    loaded->displayName = "440 Hz contract tone";

    for (int i = 0; i < samples; ++i)
    {
        const auto value = (float) (0.6 * std::sin (juce::MathConstants<double>::twoPi * frequencyHz * (double) i / sampleRate));
        loaded->audio.setSample (0, i, value);
        loaded->audio.setSample (1, i, value);
    }

    loaded->analysis = TrackAnalyzer::analyze (loaded->audio, loaded->sourceSampleRate);
    return loaded;
}

std::shared_ptr<const DeckPlayer::LoadedAudio> makeRampAudio (double seconds)
{
    auto loaded = std::make_shared<DeckPlayer::LoadedAudio>();
    const int samples = (int) std::round (sampleRate * seconds);
    loaded->audio.setSize (2, samples);
    loaded->sourceSampleRate = sampleRate;
    loaded->path = "synthetic://beat-grid-ramp.wav";
    loaded->displayName = "beat grid ramp";

    for (int i = 0; i < samples; ++i)
    {
        const auto value = (float) i / (float) samples;
        loaded->audio.setSample (0, i, value);
        loaded->audio.setSample (1, i, value);
    }

    loaded->analysis = TrackAnalyzer::analyze (loaded->audio, loaded->sourceSampleRate);
    return loaded;
}

std::shared_ptr<const DeckPlayer::LoadedAudio> makeCMajorAudio (double seconds)
{
    auto loaded = std::make_shared<DeckPlayer::LoadedAudio>();
    const int samples = (int) std::round (sampleRate * seconds);
    loaded->audio.setSize (2, samples);
    loaded->sourceSampleRate = sampleRate;
    loaded->path = "synthetic://c-major-live-key.wav";
    loaded->displayName = "C major live key contract tone";

    for (int i = 0; i < samples; ++i)
    {
        const double t = (double) i / sampleRate;
        const auto value = (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * 261.625565 * t)
                                  + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 329.627557 * t)
                                  + 0.2 * std::sin (juce::MathConstants<double>::twoPi * 391.995436 * t));
        loaded->audio.setSample (0, i, value);
        loaded->audio.setSample (1, i, value);
    }

    loaded->analysis = TrackAnalyzer::analyze (loaded->audio, loaded->sourceSampleRate);
    return loaded;
}

juce::AudioBuffer<float> renderDeck (const TimePitchDecision& decision, int numSamples)
{
    DeckPlayer deck;
    deck.prepare (sampleRate, 512, 2);
    deck.setLoadedAudio (makeSineAudio (440.0, 3.0));

    std::array<float, DeckPlayer::numStemLanes> stemGains {};
    stemGains.fill (-60.0f);
    stemGains[(size_t) DeckPlayer::fullMixStem] = 0.0f;

    juce::AudioBuffer<float> output (2, numSamples);
    output.clear();
    deck.process (output, 0, numSamples, true, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, decision, stemGains, {});
    return output;
}

void expectBeatSyncUsesPerDeckSourceBpmAndOffset()
{
    DeckPlayer deck;
    deck.prepare (sampleRate, 512, 2);
    deck.setLoadedAudio (makeRampAudio (2.0));

    std::array<float, DeckPlayer::numStemLanes> stemGains {};
    stemGains.fill (-60.0f);
    stemGains[(size_t) DeckPlayer::fullMixStem] = 0.0f;

    juce::AudioBuffer<float> output (2, 8);
    output.clear();

    // 120 source BPM means one beat is 0.5s; host beat position 1.0 should land
    // near 25% through this two-second ramp. This protects phase/grid sync from
    // silently becoming only tempo-rate matching.
    BeatSyncEngine::TrackBeatGrid grid;
    grid.userBpm = 120.0f;
    grid.syncMode = BeatSyncEngine::SyncMode::strict;
    const auto plan = BeatSyncEngine::makePlaybackPlan ({ 120.0, true, 1.0 }, grid, sampleRate, sampleRate * 2.0, true);
    deck.process (output, 0, output.getNumSamples(), true, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                  TimePitchEngine::decide ({ 1.0f, 0.0f, false }), stemGains, plan);

    const float first = output.getSample (0, 0);
    if (std::abs (first - 0.25f) > 0.07f)
        fail ("beat sync did not jump to the expected per-deck BPM beat-grid position: first=" + std::to_string (first));
}

void expectBeatSyncModesAndTempoInterpretation()
{
    TrackAnalysis analysis;
    analysis.sampleRate = sampleRate;
    analysis.detectedBpm = 96.0f;
    analysis.beatGridSamples = { 0.0, sampleRate * 0.625, sampleRate * 1.25, sampleRate * 1.875 };
    analysis.transientAnchorSamples = { sampleRate * 0.02, sampleRate * 0.65, sampleRate * 1.21 };

    const auto halfGrid = BeatSyncEngine::makeGrid (analysis, 96.0f, 0.0f, 0.0f,
                                                    BeatSyncEngine::TempoInterpretation::halfTime,
                                                    BeatSyncEngine::SyncMode::strict);
    if (std::abs (halfGrid.effectiveSourceBpm() - 48.0f) > 0.01f)
        fail ("half-time interpretation did not halve the effective source BPM");

    const auto doubleGrid = BeatSyncEngine::makeGrid (analysis, 96.0f, 0.0f, 0.0f,
                                                      BeatSyncEngine::TempoInterpretation::doubleTime,
                                                      BeatSyncEngine::SyncMode::strict);
    if (std::abs (doubleGrid.effectiveSourceBpm() - 192.0f) > 0.01f)
        fail ("double-time interpretation did not double the effective source BPM");

    auto strictGrid = BeatSyncEngine::makeGrid (analysis, 96.0f, 0.0f, 0.0f,
                                                BeatSyncEngine::TempoInterpretation::normal,
                                                BeatSyncEngine::SyncMode::strict);
    auto strict = BeatSyncEngine::makePlaybackPlan ({ 120.0, true, 4.0 }, strictGrid, sampleRate, sampleRate * 8.0, true);
    if (! strict.hasLockedSourcePosition || std::abs (strict.tempoRatio - 1.25f) > 0.001f)
        fail ("Strict Sync did not lock source position at master/source tempo ratio");

    auto freeGrid = strictGrid;
    freeGrid.syncMode = BeatSyncEngine::SyncMode::freeDrift;
    auto free = BeatSyncEngine::makePlaybackPlan ({ 120.0, true, 4.0 }, freeGrid, sampleRate, sampleRate * 8.0, true);
    if (free.hasLockedSourcePosition || std::abs (free.tempoRatio - 1.25f) > 0.001f)
        fail ("Free Drift should tempo-match without hard beat-position locking");

    auto anchorGrid = strictGrid;
    anchorGrid.syncMode = BeatSyncEngine::SyncMode::anchor;
    auto anchor = BeatSyncEngine::makePlaybackPlan ({ 120.0, true, 1.0 }, anchorGrid, sampleRate, sampleRate * 8.0, true);
    if (! anchor.hasLockedSourcePosition)
        fail ("Anchor Sync did not produce a locked source position from warp/transient anchors");
}

void expectLiveKeyWindowAnalysis()
{
    DeckPlayer deck;
    deck.prepare (sampleRate, 512, 2);
    deck.setLoadedAudio (makeCMajorAudio (10.0));

    const auto analysis = deck.analyzeFullMixAroundPlayhead (8.0);
    if (! analysis.hasKey())
        fail ("live key window analysis did not return a key");
    if (analysis.keyConfidence < 0.05f)
        fail ("live key window analysis returned very low confidence");
}

double rmsOf (const juce::AudioBuffer<float>& buffer)
{
    double sumSquares = 0.0;
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (! std::isfinite (data[i]))
                fail ("render produced a non-finite sample");
            sumSquares += (double) data[i] * (double) data[i];
            ++count;
        }
    }

    return count > 0 ? std::sqrt (sumSquares / (double) count) : 0.0;
}

int positiveZeroCrossings (const juce::AudioBuffer<float>& buffer, int skipSamples)
{
    const auto* data = buffer.getReadPointer (0);
    int crossings = 0;
    float previous = data[juce::jlimit (0, buffer.getNumSamples() - 1, skipSamples)];
    for (int i = skipSamples + 1; i < buffer.getNumSamples(); ++i)
    {
        const float current = data[i];
        if (previous <= 0.0f && current > 0.0f)
            ++crossings;
        previous = current;
    }
    return crossings;
}

void expectDecisionRouting()
{
    const auto keyLock = TimePitchEngine::decide ({ 1.5f, 0.0f, true });
    if (! keyLock.pitchPreservingActive || keyLock.usingResampleFallback)
        fail ("tempo-change Preserve request did not route to granular key-lock");
    if (! keyLock.statusLabel.containsIgnoreCase ("granular"))
        fail ("key-lock decision did not expose granular status text");

    const auto fallback = TimePitchEngine::decide ({ 1.5f, 0.0f, false });
    if (fallback.pitchPreservingActive || ! fallback.usingResampleFallback)
        fail ("tempo-change Preserve-off request did not route to resample fallback");
    if (! fallback.statusLabel.containsIgnoreCase ("fallback"))
        fail ("fallback decision did not expose fallback status text");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    expectDecisionRouting();
    expectBeatSyncUsesPerDeckSourceBpmAndOffset();
    expectBeatSyncModesAndTempoInterpretation();
    expectLiveKeyWindowAnalysis();

    const int renderSamples = (int) sampleRate;
    const int skipSamples = 8192;

    const auto keyLockTempo = renderDeck (TimePitchEngine::decide ({ 1.5f, 0.0f, true }), renderSamples);
    const auto resampleTempo = renderDeck (TimePitchEngine::decide ({ 1.5f, 0.0f, false }), renderSamples);
    const auto keyLockOctave = renderDeck (TimePitchEngine::decide ({ 1.0f, 12.0f, true }), renderSamples);

    const double keyLockRms = rmsOf (keyLockTempo);
    const double resampleRms = rmsOf (resampleTempo);
    const double octaveRms = rmsOf (keyLockOctave);
    if (keyLockRms < 0.05 || resampleRms < 0.05 || octaveRms < 0.05)
        fail ("one or more render paths produced near-silence");

    const int keyLockCrossings = positiveZeroCrossings (keyLockTempo, skipSamples);
    const int resampleCrossings = positiveZeroCrossings (resampleTempo, skipSamples);
    const int octaveCrossings = positiveZeroCrossings (keyLockOctave, skipSamples);

    if (resampleCrossings <= (int) std::round ((double) keyLockCrossings * 1.25))
        fail ("Preserve-off tempo fallback did not audibly pitch up versus granular key-lock tempo render");

    if (octaveCrossings <= (int) std::round ((double) keyLockCrossings * 1.6))
        fail ("granular key-lock pitch-shift render did not raise zero-crossing frequency enough");

    std::cout << "StemDeck granular render contract ok\n"
              << "  key_lock_tempo_rms=" << keyLockRms << " crossings=" << keyLockCrossings << '\n'
              << "  resample_tempo_rms=" << resampleRms << " crossings=" << resampleCrossings << '\n'
              << "  key_lock_octave_rms=" << octaveRms << " crossings=" << octaveCrossings << '\n';
    return 0;
}
