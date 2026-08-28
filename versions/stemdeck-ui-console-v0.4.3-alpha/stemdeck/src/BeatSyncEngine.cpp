#include "BeatSyncEngine.h"

namespace
{
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
constexpr float fallbackBpm = 120.0f;

float sanitiseBpm (float bpm) noexcept
{
    return juce::jlimit (40.0f, 240.0f, std::isfinite (bpm) && bpm > 1.0f ? bpm : fallbackBpm);
}
}

float BeatSyncEngine::TrackBeatGrid::effectiveSourceBpm() const noexcept
{
    float bpm = sanitiseBpm (userBpm > 1.0f ? userBpm : originalDetectedBpm);

    switch (interpretation)
    {
        case TempoInterpretation::halfTime:   bpm *= 0.5f; break;
        case TempoInterpretation::doubleTime: bpm *= 2.0f; break;
        case TempoInterpretation::normal:     break;
    }

    return juce::jlimit (40.0f, 240.0f, bpm);
}

juce::String BeatSyncEngine::TrackBeatGrid::interpretationLabel() const
{
    switch (interpretation)
    {
        case TempoInterpretation::halfTime:   return "half-time";
        case TempoInterpretation::doubleTime: return "double-time";
        case TempoInterpretation::normal:     break;
    }

    return "normal";
}

juce::String BeatSyncEngine::TrackBeatGrid::syncModeLabel() const
{
    switch (syncMode)
    {
        case SyncMode::groove:    return "Groove Sync";
        case SyncMode::anchor:    return "Anchor Sync";
        case SyncMode::freeDrift: return "Free Drift";
        case SyncMode::strict:    break;
    }

    return "Strict Sync";
}

BeatSyncEngine::TempoInterpretation BeatSyncEngine::tempoInterpretationFromChoice (int choiceIndex) noexcept
{
    switch (choiceIndex)
    {
        case 1:  return TempoInterpretation::halfTime;
        case 2:  return TempoInterpretation::doubleTime;
        default: break;
    }

    return TempoInterpretation::normal;
}

BeatSyncEngine::SyncMode BeatSyncEngine::syncModeFromChoice (int choiceIndex) noexcept
{
    switch (choiceIndex)
    {
        case 1:  return SyncMode::groove;
        case 2:  return SyncMode::anchor;
        case 3:  return SyncMode::freeDrift;
        default: break;
    }

    return SyncMode::strict;
}

juce::StringArray BeatSyncEngine::tempoInterpretationChoices()
{
    return { "Normal", "Half-time", "Double-time" };
}

juce::StringArray BeatSyncEngine::syncModeChoices()
{
    return { "Strict", "Groove", "Anchor", "Free Drift" };
}

BeatSyncEngine::TrackBeatGrid BeatSyncEngine::makeGrid (const TrackAnalysis& analysis,
                                                        float userBpm,
                                                        float manualGridCorrectionBeats,
                                                        float downbeatPositionBeats,
                                                        TempoInterpretation interpretation,
                                                        SyncMode syncMode)
{
    TrackBeatGrid grid;
    grid.originalDetectedBpm = analysis.detectedBpm;
    grid.userBpm = sanitiseBpm (userBpm);
    grid.manualGridCorrectionBeats = juce::jlimit (-16.0f, 16.0f, manualGridCorrectionBeats);
    grid.downbeatPositionBeats = juce::jlimit (-64.0, 64.0, (double) downbeatPositionBeats);
    grid.interpretation = interpretation;
    grid.syncMode = syncMode;
    grid.beatGridSamples = analysis.beatGridSamples;
    grid.transientAnchorSamples = analysis.transientAnchorSamples;

    const float effectiveBpm = grid.effectiveSourceBpm();
    const double beatLength = analysis.sampleRate > 1.0 && effectiveBpm > 1.0f
        ? analysis.sampleRate * 60.0 / (double) effectiveBpm
        : 0.0;

    grid.warpMarkers.reserve (grid.beatGridSamples.size() + grid.transientAnchorSamples.size());
    for (size_t i = 0; i < grid.beatGridSamples.size(); ++i)
    {
        const double sourceBeat = beatLength > 1.0 ? grid.beatGridSamples[i] / beatLength : (double) i;
        grid.warpMarkers.push_back ({ sourceBeat, sourceBeat, grid.beatGridSamples[i], false });
    }

    for (double anchorSample : grid.transientAnchorSamples)
    {
        const double sourceBeat = beatLength > 1.0 ? anchorSample / beatLength : 0.0;
        grid.warpMarkers.push_back ({ sourceBeat, sourceBeat, anchorSample, true });
    }

    std::sort (grid.warpMarkers.begin(), grid.warpMarkers.end(), [] (const WarpMarker& a, const WarpMarker& b)
    {
        if (std::abs (a.projectBeat - b.projectBeat) < 1.0e-6)
            return a.transientAnchor && ! b.transientAnchor;
        return a.projectBeat < b.projectBeat;
    });

    return grid;
}

double BeatSyncEngine::wrapPositive (double value, double length) noexcept
{
    if (length <= 1.0 || ! std::isfinite (value))
        return 0.0;

    double wrapped = std::fmod (value, length);
    if (wrapped < 0.0)
        wrapped += length;
    return wrapped;
}

double BeatSyncEngine::mapBeatThroughWarpMarkers (const TrackBeatGrid& grid,
                                                  double projectBeat,
                                                  double sourceBeatLengthSamples,
                                                  double sourceLengthSamples,
                                                  bool localGroove)
{
    if (sourceBeatLengthSamples <= 1.0 || sourceLengthSamples <= 1.0)
        return 0.0;

    if (grid.warpMarkers.size() < 2)
        return wrapPositive (projectBeat * sourceBeatLengthSamples, sourceLengthSamples);

    const auto wrappedBeat = wrapPositive (projectBeat, sourceLengthSamples / sourceBeatLengthSamples);
    const WarpMarker* previous = &grid.warpMarkers.front();
    const WarpMarker* next = nullptr;

    for (const auto& marker : grid.warpMarkers)
    {
        if (marker.projectBeat <= wrappedBeat)
            previous = &marker;
        else
        {
            next = &marker;
            break;
        }
    }

    if (next == nullptr)
        next = &grid.warpMarkers.back();

    const double span = juce::jmax (0.0001, next->projectBeat - previous->projectBeat);
    const double phase = juce::jlimit (0.0, 1.0, (wrappedBeat - previous->projectBeat) / span);
    const double interpolatedBeat = previous->sourceBeat + phase * (next->sourceBeat - previous->sourceBeat);
    const double strictSample = wrappedBeat * sourceBeatLengthSamples;
    const double warpedSample = interpolatedBeat * sourceBeatLengthSamples;

    if (localGroove)
    {
        // Groove Sync keeps transient-derived microtiming but limits correction so
        // the track never drifts far away from the master grid.
        const double maxGroovePush = sourceBeatLengthSamples * 0.20;
        const double grooveDelta = juce::jlimit (-maxGroovePush, maxGroovePush, warpedSample - strictSample);
        return wrapPositive (strictSample + grooveDelta, sourceLengthSamples);
    }

    return wrapPositive (warpedSample, sourceLengthSamples);
}

BeatSyncEngine::PlaybackPlan BeatSyncEngine::makePlaybackPlan (const TransportSync::Snapshot& transport,
                                                               const TrackBeatGrid& grid,
                                                               double referenceSampleRate,
                                                               double sourceLengthSamples,
                                                               bool globalBeatSyncEnabled,
                                                               bool wantStatus)
{
    PlaybackPlan plan;
    plan.syncMode = grid.syncMode;
    plan.masterBpm = transport.bpm > 1.0 ? transport.bpm : 120.0;
    plan.masterBeatPosition = transport.ppqPosition;
    plan.sourceBpm = grid.effectiveSourceBpm();
    plan.tempoRatio = juce::jlimit (0.25f, 4.0f, (float) plan.masterBpm / plan.sourceBpm);
    plan.syncEnabled = globalBeatSyncEnabled && transport.hostPlaying && std::isfinite (transport.ppqPosition);

    const double sourceBeatLength = referenceSampleRate > 1.0 && plan.sourceBpm > 1.0f
        ? referenceSampleRate * 60.0 / (double) plan.sourceBpm
        : 0.0;

    const double correctedProjectBeat = transport.ppqPosition
                                      + (double) grid.manualGridCorrectionBeats
                                      - grid.downbeatPositionBeats;

    if (plan.syncEnabled && sourceBeatLength > 1.0 && sourceLengthSamples > 1.0)
    {
        switch (grid.syncMode)
        {
            case SyncMode::freeDrift:
                plan.hasLockedSourcePosition = false;
                break;

            case SyncMode::groove:
                plan.lockedSourcePositionSamples = mapBeatThroughWarpMarkers (grid, correctedProjectBeat, sourceBeatLength, sourceLengthSamples, true);
                plan.hasLockedSourcePosition = true;
                break;

            case SyncMode::anchor:
                plan.lockedSourcePositionSamples = mapBeatThroughWarpMarkers (grid, correctedProjectBeat, sourceBeatLength, sourceLengthSamples, false);
                plan.hasLockedSourcePosition = true;
                break;

            case SyncMode::strict:
                plan.lockedSourcePositionSamples = wrapPositive (correctedProjectBeat * sourceBeatLength, sourceLengthSamples);
                plan.hasLockedSourcePosition = true;
                break;
        }
    }

    // The status label is UI-only and allocates several juce::Strings; the audio
    // thread passes wantStatus=false so processBlock stays allocation-free.
    if (wantStatus)
    {
        juce::String mode;
        switch (grid.syncMode)
        {
            case SyncMode::groove:    mode = "Groove Sync"; break;
            case SyncMode::anchor:    mode = "Anchor Sync"; break;
            case SyncMode::freeDrift: mode = "Free Drift"; break;
            case SyncMode::strict:    mode = "Strict Sync"; break;
        }

        plan.statusLabel = mode + u8 (" \xe2\x80\xa2 ") + juce::String (plan.sourceBpm, 1) + u8 ("\xe2\x86\x92") + juce::String (plan.masterBpm, 1) + u8 (" BPM \xe2\x80\xa2 ") + grid.interpretationLabel();
        if (! plan.syncEnabled)
            plan.statusLabel += u8 (" \xe2\x80\xa2 armed");
        else if (plan.hasLockedSourcePosition)
            plan.statusLabel += u8 (" \xe2\x80\xa2 grid-locked");
        else
            plan.statusLabel += u8 (" \xe2\x80\xa2 tempo drift");
    }

    return plan;
}
