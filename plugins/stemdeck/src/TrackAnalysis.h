#pragma once

#include <JuceHeader.h>

struct TrackAnalysis
{
    double sampleRate = 44100.0;
    double durationSeconds = 0.0;
    float rmsDb = -100.0f;
    float detectedBpm = 0.0f;
    double downbeatSample = 0.0;
    int    beatsPerBar = 4;                                   // detected meter (3 or 4)
    std::vector<double> beatGridSamples;
    std::vector<double> transientAnchorSamples;
    // Tempo curve: (samplePosition, localBpm) for tracks that speed up / slow down.
    // Flat for constant-tempo material. detectedBpm is its median.
    std::vector<std::pair<double, float>> tempoCurve;
    int keyPitchClass = -1;
    bool keyIsMinor = false;
    float keyConfidence = 0.0f;

    // Which engine produced each result — user-facing (§A3/§B3: the readout must say
    // whether the neural model or the DSP fallback won, and flag low confidence).
    enum class Source { dsp = 0, neural = 1 };
    Source keySource  = Source::dsp;
    Source beatSource = Source::dsp;
    // True when the winning key result sits below the trust floor: the UI shows it
    // italic/amber rather than presenting a guess as fact.
    bool keyLowConfidence = false;

    // ── §6.2 analysis pinning (A1) ───────────────────────────────────────────
    // Which ANALYSIS of this material this is. 1 = the first-pass DSP result the deck
    // becomes playable on; each background refinement publishes a higher version.
    // A replay must use the version that was in force during the live pass, so the
    // cache stores every version rather than overwriting — refinement lands at an
    // unpredictable moment (measured 0.6-5s), and recomputing makes replay diverge
    // intermittently rather than cleanly.
    uint32_t analysisVersion = 1;

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
