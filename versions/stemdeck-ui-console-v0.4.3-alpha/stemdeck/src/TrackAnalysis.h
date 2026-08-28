#pragma once

#include <JuceHeader.h>

struct TrackAnalysis
{
    double sampleRate = 44100.0;
    double durationSeconds = 0.0;
    float rmsDb = -100.0f;
    float detectedBpm = 0.0f;
    double downbeatSample = 0.0;
    std::vector<double> beatGridSamples;
    std::vector<double> transientAnchorSamples;
    int keyPitchClass = -1;
    bool keyIsMinor = false;
    float keyConfidence = 0.0f;

    bool hasKey() const noexcept { return keyPitchClass >= 0 && keyPitchClass < 12 && keyConfidence > 0.0f; }
    juce::String keyName() const;
};

class TrackAnalyzer
{
public:
    static TrackAnalysis analyze (const juce::AudioBuffer<float>& audio, double sampleRate);
    static juce::String pitchClassName (int pitchClass);
    static int nearestSemitoneDelta (int fromPitchClass, int toPitchClass) noexcept;

private:
    static float goertzelPower (const juce::AudioBuffer<float>& audio, int startSample, int numSamples,
                                double sampleRate, double frequency);
};
