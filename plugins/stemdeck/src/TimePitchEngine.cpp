#include "TimePitchEngine.h"

TimePitchDecision TimePitchEngine::decide (const TimePitchRequest& request)
{
    TimePitchDecision decision;
    const float tempo = juce::jlimit (0.25f, 4.0f, request.tempoRatio);
    const float pitch = juce::jlimit (-24.0f, 24.0f, request.pitchSemitones);
    const float pitchRatio = std::pow (2.0f, pitch / 12.0f);
    const bool hasTempoChange = std::abs (tempo - 1.0f) > 0.001f;
    const bool hasPitchChange = std::abs (pitch) > 0.001f;
    const bool needsTransform = hasTempoChange || hasPitchChange;

    decision.tempoRatio = tempo;
    decision.pitchRatio = pitchRatio;
    decision.pitchSemitones = pitch;
    decision.sourceRateMultiplier = juce::jlimit (0.125f, 8.0f, tempo * pitchRatio);
    decision.pitchPreservingActive = request.wantsPitchPreservation && needsTransform;
    decision.usingResampleFallback = ! decision.pitchPreservingActive && needsTransform;

    if (decision.pitchPreservingActive)
#if SATTARI_STEMDECK_HAS_RUBBERBAND
        decision.statusLabel = hasPitchChange ? "Rubber Band key-lock + pitch" : "Rubber Band key-lock";
#else
        decision.statusLabel = hasPitchChange ? "granular key-lock + pitch" : "granular key-lock";
#endif
    else if (decision.usingResampleFallback)
        decision.statusLabel = "resample fallback";
    else
        decision.statusLabel = request.wantsPitchPreservation ? "granular key-lock ready" : "resample";

    return decision;
}
