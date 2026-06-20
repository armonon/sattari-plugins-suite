#include "WarpEngine.h"

#if SATTARI_STEMDECK_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace
{
class GranularFallbackBackend final : public WarpEngine::TimeStretchBackend
{
public:
    WarpEngine::BackendDescriptor describe() const override
    {
        return { "StemDeck realtime granular fallback",
                 WarpEngine::BackendKind::granularFallback,
                 true,
                 true,
                 "Runs inside the deck renderer without allocation or disk I/O; Rubber Band can replace this backend when linked." };
    }

    void prepare (double, int, int) override {}
    void reset() override {}
};

#if SATTARI_STEMDECK_HAS_RUBBERBAND
class RubberBandBackend final : public WarpEngine::TimeStretchBackend
{
public:
    WarpEngine::BackendDescriptor describe() const override
    {
        return { "Rubber Band R3", WarpEngine::BackendKind::rubberBand, true, true,
                 "High-quality pitch-preserving time stretch backend. Instances are prepared off the audio path and swapped in by the warp layer." };
    }

    void prepare (double sampleRate, int, int channels) override
    {
        stretcher = std::make_unique<RubberBand::RubberBandStretcher> (
            sampleRate,
            (size_t) juce::jmax (1, channels),
            RubberBand::RubberBandStretcher::OptionProcessRealTime
              | RubberBand::RubberBandStretcher::OptionEngineFiner
              | RubberBand::RubberBandStretcher::OptionPitchHighConsistency);
    }

    void reset() override
    {
        if (stretcher)
            stretcher->reset();
    }

private:
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
};
#endif
}

WarpEngine::WarpEngine()
{
#if SATTARI_STEMDECK_HAS_RUBBERBAND
    backend = std::make_unique<RubberBandBackend>();
#else
    backend = std::make_unique<GranularFallbackBackend>();
#endif
    selectedBackend = backend->describe();
}

void WarpEngine::prepare (double sampleRate, int maxBlockSize, int channels)
{
    if (backend)
        backend->prepare (sampleRate, maxBlockSize, channels);
    selectedBackend = backend ? backend->describe()
                              : BackendDescriptor { "No time-stretch backend", BackendKind::resampleFallback, false, true, {} };
}

void WarpEngine::reset()
{
    if (backend)
        backend->reset();
}

WarpEngine::BackendDescriptor WarpEngine::getSelectedBackend() const
{
    return selectedBackend;
}

juce::String WarpEngine::getStatusSummary() const
{
    auto backendInfo = getSelectedBackend();
    juce::String text = backendInfo.name;
    text += backendInfo.available ? " ready" : " unavailable";
    if (backendInfo.notes.isNotEmpty())
        text += " — " + backendInfo.notes;
    return text;
}

WarpEngine::Plan WarpEngine::makePlan (const TransportSync::Snapshot& transport,
                                       const BeatSyncEngine::TrackBeatGrid& grid,
                                       double sourceSampleRate,
                                       double sourceLengthSamples,
                                       float pitchSemitones,
                                       bool wantsPitchPreservation,
                                       bool beatSyncEnabled) const
{
    Plan plan;
    plan.masterBpm = transport.bpm > 1.0 ? transport.bpm : 120.0;
    plan.beatSync = BeatSyncEngine::makePlaybackPlan (transport,
                                                       grid,
                                                       sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0,
                                                       juce::jmax (0.0, sourceLengthSamples),
                                                       beatSyncEnabled);

    plan.deck.sourceBpm = plan.beatSync.sourceBpm;
    plan.deck.beatGrid = grid;
    plan.deck.pitchSemitones = juce::jlimit (-24.0f, 24.0f, pitchSemitones);
    plan.deck.tempoRatio = juce::jlimit (0.25f, 4.0f, plan.beatSync.tempoRatio);
    plan.deck.syncMode = grid.syncMode;
    plan.deck.syncEnabled = beatSyncEnabled;
    plan.deck.pitchPreserving = wantsPitchPreservation;
    plan.deck.sourceSampleRate = sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0;
    plan.deck.sourceLengthSamples = juce::jmax (0.0, sourceLengthSamples);

    plan.timePitch = TimePitchEngine::decide ({ plan.deck.tempoRatio,
                                                plan.deck.pitchSemitones,
                                                wantsPitchPreservation });
    plan.backend = getSelectedBackend();

    plan.statusLabel = "Warp: " + plan.beatSync.statusLabel
                     + " • tempo x" + juce::String (plan.timePitch.tempoRatio, 3)
                     + " • pitch " + (plan.deck.pitchSemitones >= 0.0f ? "+" : "") + juce::String (plan.deck.pitchSemitones, 2) + " st"
                     + " • " + plan.timePitch.statusLabel
                     + " • backend: " + plan.backend.name;
    return plan;
}
