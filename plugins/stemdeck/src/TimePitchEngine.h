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
    // const char*, NOT juce::String: decide() runs on the AUDIO THREAD every block,
    // and every value assigned to it is a string literal. Assigning a literal to a
    // juce::String allocates — this was the second-largest RT-safety violation after
    // the cfSide property read. Static storage costs nothing.
    const char* statusLabel = "";
};

class TimePitchEngine
{
public:
    static TimePitchDecision decide (const TimePitchRequest& request);
};
