#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>

struct VoxKeyPitchFrame
{
    bool voiced = false;
    float frequencyHz = 0.0f;
    float confidence = 0.0f;
    float rms = 0.0f;
};

// Lightweight real-time YIN-style pitch detector for monophonic vocal input.
// It is intentionally allocation-free in processBlock after prepare().
class VoxKeyPitchDetector final
{
public:
    VoxKeyPitchDetector() = default;
    void prepare (double newSampleRate, int maxBlockSize);
    void reset();

    VoxKeyPitchFrame analyseBlock (const float* monoSamples,
                                   int numSamples,
                                   float sensitivity,
                                   float confidenceThreshold,
                                   float smoothing);

private:
    float runYin (int sampleCount, float threshold, float& confidence) noexcept;
    float parabolicTau (int tau) const noexcept;

    double sampleRate = 44100.0;
    int analysisSize = 1024;
    int writePosition = 0;
    bool bufferFilled = false;
    float smoothedFrequency = 0.0f;
    std::vector<float> ring;
    std::vector<float> window;
    std::vector<float> yin;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxKeyPitchDetector)
};
