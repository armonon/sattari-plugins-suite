#include "PitchDetector.h"

namespace
{
constexpr float minFrequencyHz = 70.0f;
constexpr float maxFrequencyHz = 1200.0f;

float computeRms (const float* samples, int count) noexcept
{
    if (samples == nullptr || count <= 0)
        return 0.0f;

    double sum = 0.0;
    for (int i = 0; i < count; ++i)
        sum += (double) samples[i] * (double) samples[i];

    return std::sqrt ((float) (sum / (double) count));
}
}

void VoxKeyPitchDetector::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    analysisSize = juce::jlimit (512, 4096, juce::nextPowerOfTwo (juce::jmax (1024, maxBlockSize * 4)));
    ring.assign ((size_t) analysisSize, 0.0f);
    window.assign ((size_t) analysisSize, 0.0f);
    yin.assign ((size_t) (analysisSize / 2 + 2), 0.0f);
    reset();
}

void VoxKeyPitchDetector::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (window.begin(), window.end(), 0.0f);
    std::fill (yin.begin(), yin.end(), 0.0f);
    writePosition = 0;
    bufferFilled = false;
    smoothedFrequency = 0.0f;
}

VoxKeyPitchFrame VoxKeyPitchDetector::analyseBlock (const float* monoSamples,
                                                    int numSamples,
                                                    float sensitivity,
                                                    float confidenceThreshold,
                                                    float smoothing)
{
    VoxKeyPitchFrame frame;
    if (monoSamples == nullptr || numSamples <= 0 || ring.empty())
        return frame;

    frame.rms = computeRms (monoSamples, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        ring[(size_t) writePosition] = monoSamples[i];
        writePosition = (writePosition + 1) % analysisSize;
        if (writePosition == 0)
            bufferFilled = true;
    }

    const float gate = juce::jmap (juce::jlimit (0.0f, 1.0f, sensitivity), 0.035f, 0.0035f);
    if (! bufferFilled || frame.rms < gate)
    {
        smoothedFrequency = 0.0f;
        return frame;
    }

    for (int i = 0; i < analysisSize; ++i)
        window[(size_t) i] = ring[(size_t) ((writePosition + i) % analysisSize)];

    float confidence = 0.0f;
    const float threshold = juce::jlimit (0.05f, 0.45f, 0.32f - sensitivity * 0.22f);
    const float detected = runYin (analysisSize, threshold, confidence);
    frame.confidence = confidence;

    if (detected <= 0.0f || confidence < confidenceThreshold)
    {
        smoothedFrequency = 0.0f;
        return frame;
    }

    const float smooth = juce::jlimit (0.0f, 0.98f, smoothing);
    if (smoothedFrequency <= 0.0f)
        smoothedFrequency = detected;
    else
        smoothedFrequency = smooth * smoothedFrequency + (1.0f - smooth) * detected;

    frame.frequencyHz = smoothedFrequency;
    frame.voiced = true;
    return frame;
}

float VoxKeyPitchDetector::runYin (int sampleCount, float threshold, float& confidence) noexcept
{
    const int tauMin = juce::jmax (2, (int) std::floor (sampleRate / maxFrequencyHz));
    const int tauMax = juce::jmin ((int) yin.size() - 1, (int) std::ceil (sampleRate / minFrequencyHz));
    if (tauMax <= tauMin + 2)
        return 0.0f;

    std::fill (yin.begin(), yin.end(), 0.0f);

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0.0;
        const int limit = sampleCount - tau;
        for (int i = 0; i < limit; ++i)
        {
            const float delta = window[(size_t) i] - window[(size_t) (i + tau)];
            sum += (double) delta * (double) delta;
        }
        yin[(size_t) tau] = (float) sum;
    }

    yin[0] = 1.0f;
    double runningSum = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        runningSum += yin[(size_t) tau];
        yin[(size_t) tau] = runningSum > 0.0 ? (float) ((double) yin[(size_t) tau] * (double) tau / runningSum) : 1.0f;
    }

    int chosenTau = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (yin[(size_t) tau] < threshold)
        {
            while (tau + 1 <= tauMax && yin[(size_t) (tau + 1)] < yin[(size_t) tau])
                ++tau;
            chosenTau = tau;
            break;
        }
    }

    if (chosenTau < 0)
    {
        float best = 1.0f;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (yin[(size_t) tau] < best)
            {
                best = yin[(size_t) tau];
                chosenTau = tau;
            }

        if (chosenTau < 0 || best > threshold * 1.45f)
            return 0.0f;
    }

    const float refinedTau = parabolicTau (chosenTau);
    if (refinedTau <= 0.0f)
        return 0.0f;

    confidence = juce::jlimit (0.0f, 1.0f, 1.0f - yin[(size_t) chosenTau]);
    return (float) sampleRate / refinedTau;
}

float VoxKeyPitchDetector::parabolicTau (int tau) const noexcept
{
    if (tau <= 1 || tau + 1 >= (int) yin.size())
        return (float) tau;

    const float left = yin[(size_t) (tau - 1)];
    const float centre = yin[(size_t) tau];
    const float right = yin[(size_t) (tau + 1)];
    const float denom = left - 2.0f * centre + right;
    if (std::abs (denom) < 1.0e-9f)
        return (float) tau;

    return (float) tau + 0.5f * (left - right) / denom;
}
