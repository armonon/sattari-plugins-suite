#pragma once

#include <JuceHeader.h>
#include <memory>
#include "BeatSyncEngine.h"
#include "TimePitchEngine.h"
#include "TransportSync.h"

// WarpEngine is the per-deck timing contract for StemDeck's DJ/DAW layer.
// It owns only lightweight state/decisions used by the audio callback. Heavy
// analysis, Rubber Band setup, plugin scanning, and stem generation must happen
// outside processBlock and publish immutable snapshots into this interface.
class WarpEngine
{
public:
    enum class BackendKind
    {
        rubberBand,
        granularFallback,
        resampleFallback
    };

    struct BackendDescriptor
    {
        juce::String name;
        BackendKind kind = BackendKind::granularFallback;
        bool available = false;
        bool realtimeSafe = true;
        juce::String notes;
    };

    class TimeStretchBackend
    {
    public:
        virtual ~TimeStretchBackend() = default;
        virtual BackendDescriptor describe() const = 0;
        virtual void prepare (double sampleRate, int maxBlockSize, int channels) = 0;
        virtual void reset() = 0;
    };

    struct DeckWarpState
    {
        float sourceBpm = 120.0f;
        BeatSyncEngine::TrackBeatGrid beatGrid;
        float pitchSemitones = 0.0f;
        float tempoRatio = 1.0f;
        BeatSyncEngine::SyncMode syncMode = BeatSyncEngine::SyncMode::strict;
        bool syncEnabled = false;
        bool pitchPreserving = false;
        double sourceSampleRate = 44100.0;
        double sourceLengthSamples = 0.0;
    };

    struct Plan
    {
        DeckWarpState deck;
        BeatSyncEngine::PlaybackPlan beatSync;
        TimePitchDecision timePitch;
        BackendDescriptor backend;
        double masterBpm = 120.0;
        juce::String statusLabel;
    };

    WarpEngine();

    void prepare (double sampleRate, int maxBlockSize, int channels);
    void reset();

    BackendDescriptor getSelectedBackend() const;
    juce::String getStatusSummary() const;

    Plan makePlan (const TransportSync::Snapshot& transport,
                   const BeatSyncEngine::TrackBeatGrid& grid,
                   double sourceSampleRate,
                   double sourceLengthSamples,
                   float pitchSemitones,
                   bool wantsPitchPreservation,
                   bool beatSyncEnabled) const;

private:
    std::unique_ptr<TimeStretchBackend> backend;
    BackendDescriptor selectedBackend;
};
