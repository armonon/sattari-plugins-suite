#include "TrackAnalysis.h"

namespace
{
constexpr std::array<const char*, 12> noteNames { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
constexpr std::array<float, 12> majorProfile { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
constexpr std::array<float, 12> minorProfile { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

int midiNoteToPitchClass (int midiNote) noexcept
{
    return (midiNote % 12 + 12) % 12;
}

double midiNoteToFrequency (int midiNote) noexcept
{
    return 440.0 * std::pow (2.0, ((double) midiNote - 69.0) / 12.0);
}

float normalisedCorrelation (const std::array<float, 12>& chroma, const std::array<float, 12>& profile, int transposition)
{
    float chromaMean = 0.0f;
    float profileMean = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        chromaMean += chroma[(size_t) i];
        profileMean += profile[(size_t) i];
    }
    chromaMean /= 12.0f;
    profileMean /= 12.0f;

    float numerator = 0.0f;
    float chromaEnergy = 0.0f;
    float profileEnergy = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float c = chroma[(size_t) ((i + transposition) % 12)] - chromaMean;
        const float p = profile[(size_t) i] - profileMean;
        numerator += c * p;
        chromaEnergy += c * c;
        profileEnergy += p * p;
    }

    const float denom = std::sqrt (chromaEnergy * profileEnergy);
    return denom > 1.0e-9f ? numerator / denom : 0.0f;
}

std::vector<float> makeOnsetEnvelope (const juce::AudioBuffer<float>& audio, int hopSize)
{
    std::vector<float> envelope;
    if (audio.getNumSamples() <= hopSize || audio.getNumChannels() <= 0)
        return envelope;

    envelope.reserve ((size_t) (audio.getNumSamples() / hopSize + 1));
    float previousEnergy = 0.0f;
    for (int start = 0; start < audio.getNumSamples(); start += hopSize)
    {
        const int end = juce::jmin (start + hopSize, audio.getNumSamples());
        double energy = 0.0;
        for (int i = start; i < end; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                mono += audio.getSample (ch, i);
            mono /= (float) audio.getNumChannels();
            energy += std::abs (mono);
        }

        const float frameEnergy = (float) (energy / (double) juce::jmax (1, end - start));
        envelope.push_back (juce::jmax (0.0f, frameEnergy - previousEnergy));
        previousEnergy = frameEnergy;
    }

    return envelope;
}

float estimateTempoFromOnsets (const std::vector<float>& onsetEnvelope, double sampleRate, int hopSize)
{
    if (onsetEnvelope.size() < 16 || sampleRate <= 0.0 || hopSize <= 0)
        return 0.0f;

    float bestScore = 0.0f;
    int bestLag = 0;
    const int minLag = juce::jmax (1, (int) std::floor (sampleRate * 60.0 / (180.0 * (double) hopSize)));
    const int maxLag = juce::jmax (minLag + 1, (int) std::ceil (sampleRate * 60.0 / (70.0 * (double) hopSize)));

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double score = 0.0;
        for (size_t i = (size_t) lag; i < onsetEnvelope.size(); ++i)
            score += (double) onsetEnvelope[i] * (double) onsetEnvelope[i - (size_t) lag];

        if ((float) score > bestScore)
        {
            bestScore = (float) score;
            bestLag = lag;
        }
    }

    if (bestLag <= 0 || bestScore <= 1.0e-8f)
        return 0.0f;

    return juce::jlimit (40.0f, 240.0f, (float) (sampleRate * 60.0 / ((double) bestLag * (double) hopSize)));
}

std::vector<double> findTransientAnchors (const std::vector<float>& onsetEnvelope, double sampleRate, int hopSize)
{
    std::vector<double> anchors;
    if (onsetEnvelope.size() < 3 || sampleRate <= 0.0)
        return anchors;

    float mean = 0.0f;
    for (float value : onsetEnvelope)
        mean += value;
    mean /= (float) onsetEnvelope.size();

    float variance = 0.0f;
    for (float value : onsetEnvelope)
        variance += (value - mean) * (value - mean);
    const float threshold = mean + std::sqrt (variance / (float) onsetEnvelope.size()) * 1.35f;
    const double minSpacingSamples = sampleRate * 0.10;
    double lastAnchor = -minSpacingSamples;

    for (size_t i = 1; i + 1 < onsetEnvelope.size(); ++i)
    {
        const float current = onsetEnvelope[i];
        if (current <= threshold || current < onsetEnvelope[i - 1] || current < onsetEnvelope[i + 1])
            continue;

        const double sample = (double) i * (double) hopSize;
        if (sample - lastAnchor >= minSpacingSamples)
        {
            anchors.push_back (sample);
            lastAnchor = sample;
        }

        if (anchors.size() >= 256)
            break;
    }

    return anchors;
}

std::vector<double> makeBeatGrid (double durationSamples, double sampleRate, float bpm, double downbeatSample)
{
    std::vector<double> beats;
    if (durationSamples <= 1.0 || sampleRate <= 0.0 || bpm <= 1.0f)
        return beats;

    const double beatLength = sampleRate * 60.0 / (double) bpm;
    if (beatLength <= 1.0)
        return beats;

    double firstBeat = downbeatSample;
    while (firstBeat > 0.0)
        firstBeat -= beatLength;
    for (double sample = firstBeat; sample < durationSamples; sample += beatLength)
        if (sample >= 0.0)
            beats.push_back (sample);

    return beats;
}
} // namespace

juce::String TrackAnalysis::keyName() const
{
    if (! hasKey())
        return "Unknown";

    return TrackAnalyzer::pitchClassName (keyPitchClass) + (keyIsMinor ? " minor" : " major");
}

juce::String TrackAnalyzer::pitchClassName (int pitchClass)
{
    return noteNames[(size_t) juce::jlimit (0, 11, pitchClass)];
}

int TrackAnalyzer::nearestSemitoneDelta (int fromPitchClass, int toPitchClass) noexcept
{
    if (fromPitchClass < 0 || toPitchClass < 0)
        return 0;

    int delta = (toPitchClass - fromPitchClass) % 12;
    if (delta > 6)
        delta -= 12;
    if (delta < -6)
        delta += 12;
    return delta;
}

float TrackAnalyzer::goertzelPower (const juce::AudioBuffer<float>& audio, int startSample, int numSamples,
                                    double sampleRate, double frequency)
{
    if (audio.getNumChannels() == 0 || numSamples <= 0 || sampleRate <= 0.0 || frequency <= 0.0)
        return 0.0f;

    const double omega = juce::MathConstants<double>::twoPi * frequency / sampleRate;
    const double coeff = 2.0 * std::cos (omega);
    double q0 = 0.0;
    double q1 = 0.0;
    double q2 = 0.0;

    const int last = juce::jmin (startSample + numSamples, audio.getNumSamples());
    const int channelCount = audio.getNumChannels();
    for (int i = startSample; i < last; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < channelCount; ++ch)
            sample += audio.getSample (ch, i);
        sample /= (float) channelCount;

        const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (double) (i - startSample) / (double) juce::jmax (1, numSamples - 1));
        q0 = coeff * q1 - q2 + (double) sample * window;
        q2 = q1;
        q1 = q0;
    }

    return (float) juce::jmax (0.0, q1 * q1 + q2 * q2 - coeff * q1 * q2);
}

TrackAnalysis TrackAnalyzer::analyze (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    TrackAnalysis result;
    if (audio.getNumSamples() <= 0 || audio.getNumChannels() <= 0 || sampleRate <= 0.0)
        return result;

    result.sampleRate = sampleRate;
    result.durationSeconds = (double) audio.getNumSamples() / sampleRate;

    double sumSquares = 0.0;
    int64_t count = 0;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        const auto* data = audio.getReadPointer (ch);
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            const double s = data[i];
            sumSquares += s * s;
        }
        count += audio.getNumSamples();
    }
    const double rms = count > 0 ? std::sqrt (sumSquares / (double) count) : 0.0;
    result.rmsDb = juce::Decibels::gainToDecibels ((float) rms, -100.0f);

    constexpr int onsetHopSize = 512;
    const auto onsetEnvelope = makeOnsetEnvelope (audio, onsetHopSize);
    result.detectedBpm = estimateTempoFromOnsets (onsetEnvelope, sampleRate, onsetHopSize);
    result.transientAnchorSamples = findTransientAnchors (onsetEnvelope, sampleRate, onsetHopSize);
    if (! result.transientAnchorSamples.empty())
        result.downbeatSample = result.transientAnchorSamples.front();
    result.beatGridSamples = makeBeatGrid ((double) audio.getNumSamples(), sampleRate, result.detectedBpm, result.downbeatSample);

    const int analysisSamples = juce::jmin (audio.getNumSamples(), (int) std::round (sampleRate * 24.0));
    constexpr int windowSize = 4096;
    constexpr int hopSize = 4096;
    if (analysisSamples < windowSize)
        return result;

    std::array<float, 12> chroma {};
    int windows = 0;
    for (int start = 0; start + windowSize <= analysisSamples; start += hopSize)
    {
        ++windows;
        for (int midi = 36; midi <= 84; ++midi)
        {
            const double frequency = midiNoteToFrequency (midi);
            if (frequency >= sampleRate * 0.45)
                continue;

            chroma[(size_t) midiNoteToPitchClass (midi)] += std::sqrt (goertzelPower (audio, start, windowSize, sampleRate, frequency));
        }
    }

    if (windows == 0)
        return result;

    float total = 0.0f;
    for (auto& value : chroma)
    {
        value = std::log1p (value / (float) windows);
        total += value;
    }

    if (total <= 1.0e-6f)
        return result;

    float bestScore = -2.0f;
    float secondScore = -2.0f;
    int bestKey = -1;
    bool bestMinor = false;

    for (int key = 0; key < 12; ++key)
    {
        const float major = normalisedCorrelation (chroma, majorProfile, key);
        const float minor = normalisedCorrelation (chroma, minorProfile, key);

        auto consider = [&] (float score, bool isMinor)
        {
            if (score > bestScore)
            {
                secondScore = bestScore;
                bestScore = score;
                bestKey = key;
                bestMinor = isMinor;
            }
            else if (score > secondScore)
            {
                secondScore = score;
            }
        };

        consider (major, false);
        consider (minor, true);
    }

    if (bestKey >= 0)
    {
        result.keyPitchClass = bestKey;
        result.keyIsMinor = bestMinor;
        result.keyConfidence = juce::jlimit (0.0f, 1.0f, (bestScore - secondScore + 0.15f) / 0.5f);
    }

    return result;
}
