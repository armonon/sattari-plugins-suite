#pragma once

#include <JuceHeader.h>

#ifndef SATTARI_STEMDECK_HAS_RUBBERBAND
#define SATTARI_STEMDECK_HAS_RUBBERBAND 0
#endif

struct TimePitchRequest
{
    float tempoRatio = 1.0f;
    float pitchSemitones = 0.0f;
    bool wantsPitchPreservation = false;
};

struct TimePitchDecision
{
    float tempoRatio = 1.0f;
    float pitchRatio = 1.0f;
    float pitchSemitones = 0.0f;
    float sourceRateMultiplier = 1.0f;
    bool pitchPreservingActive = false;
    bool usingResampleFallback = false;
    juce::String statusLabel;
};

class TimePitchEngine
{
public:
    static TimePitchDecision decide (const TimePitchRequest& request);
};
