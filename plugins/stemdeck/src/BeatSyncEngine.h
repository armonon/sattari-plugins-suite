#pragma once

#include <JuceHeader.h>
#include "TrackAnalysis.h"
#include "TransportSync.h"

class BeatSyncEngine
{
public:
    enum class TempoInterpretation
    {
        normal = 0,
        halfTime,
        doubleTime
    };

    enum class SyncMode
    {
        strict = 0,
        groove,
        anchor,
        freeDrift
    };

    struct WarpMarker
    {
        double projectBeat = 0.0;
        double sourceBeat = 0.0;
        double sourceSample = 0.0;
        bool transientAnchor = false;
    };

    struct TrackBeatGrid
    {
        float originalDetectedBpm = 0.0f;
        float userBpm = 120.0f;
        double downbeatPositionBeats = 0.0;
        float manualGridCorrectionBeats = 0.0f;
        TempoInterpretation interpretation = TempoInterpretation::normal;
        SyncMode syncMode = SyncMode::strict;
        std::vector<double> beatGridSamples;
        std::vector<double> transientAnchorSamples;
        std::vector<WarpMarker> warpMarkers;

        float effectiveSourceBpm() const noexcept;
        juce::String interpretationLabel() const;
        juce::String syncModeLabel() const;
    };

    struct PlaybackPlan
    {
        bool syncEnabled = false;
        SyncMode syncMode = SyncMode::strict;
        double masterBpm = 120.0;
        double masterBeatPosition = 0.0;
        float sourceBpm = 120.0f;
        float tempoRatio = 1.0f;
        double lockedSourcePositionSamples = 0.0;
        bool hasLockedSourcePosition = false;
        juce::String statusLabel;
    };

    static TrackBeatGrid makeGrid (const TrackAnalysis& analysis,
                                   float userBpm,
                                   float manualGridCorrectionBeats,
                                   float downbeatPositionBeats,
                                   TempoInterpretation interpretation,
                                   SyncMode syncMode);

    static PlaybackPlan makePlaybackPlan (const TransportSync::Snapshot& transport,
                                          const TrackBeatGrid& grid,
                                          double referenceSampleRate,
                                          double sourceLengthSamples,
                                          bool globalBeatSyncEnabled,
                                          bool wantStatus = true);   // false on the audio thread — skip String build

    static TempoInterpretation tempoInterpretationFromChoice (int choiceIndex) noexcept;
    static SyncMode syncModeFromChoice (int choiceIndex) noexcept;
    static juce::StringArray tempoInterpretationChoices();
    static juce::StringArray syncModeChoices();

private:
    static double wrapPositive (double value, double length) noexcept;
    static double mapBeatThroughWarpMarkers (const TrackBeatGrid& grid,
                                             double projectBeat,
                                             double sourceBeatLengthSamples,
                                             double sourceLengthSamples,
                                             bool localGroove);
};
