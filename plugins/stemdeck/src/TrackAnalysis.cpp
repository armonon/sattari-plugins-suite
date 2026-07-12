#include "TrackAnalysis.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
constexpr std::array<const char*, 12> noteNames { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
constexpr std::array<float, 12> majorProfile { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
constexpr std::array<float, 12> minorProfile { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

int midiNoteToPitchClass (int midiNote) noexcept
{
    return (midiNote % 12 + 12) % 12;
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

// Multiband spectral-flux onset envelope: log-compressed magnitude flux summed
// across bins with a low(kick)/high(hat)-emphasising band weighting. Far more
// robust to timbre than the old mono energy difference — captures kick and hat
// onsets that a single broadband energy measure smears together.
std::vector<float> makeOnsetEnvelope (const juce::AudioBuffer<float>& audio, int hopSize)
{
    std::vector<float> envelope;
    const int n = audio.getNumSamples();
    if (n <= hopSize || audio.getNumChannels() <= 0)
        return envelope;

    constexpr int fftOrder = 11;              // 2048-point
    constexpr int fftSize  = 1 << fftOrder;
    const int     numBins  = fftSize / 2 + 1;
    juce::dsp::FFT fft (fftOrder);

    // Mono downmix.
    std::vector<float> mono ((size_t) n, 0.0f);
    const int ch = audio.getNumChannels();
    for (int c = 0; c < ch; ++c)
    {
        const auto* d = audio.getReadPointer (c);
        for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i] / (float) ch;
    }

    std::vector<float> win ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));

    std::vector<float> prevMag ((size_t) numBins, 0.0f);
    std::vector<float> fftData ((size_t) (2 * fftSize), 0.0f);

    envelope.reserve ((size_t) (n / hopSize + 1));
    bool first = true;
    for (int start = 0; start + fftSize <= n; start += hopSize)
    {
        std::fill (fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = mono[(size_t) (start + i)] * win[(size_t) i];
        fft.performRealOnlyForwardTransform (fftData.data());

        float flux = 0.0f;
        for (int k = 0; k < numBins; ++k)
        {
            const float re  = fftData[(size_t) (2 * k)];
            const float im  = fftData[(size_t) (2 * k + 1)];
            const float mag = std::log1p (std::sqrt (re * re + im * im));   // log-compressed
            const float diff = mag - prevMag[(size_t) k];
            if (! first && diff > 0.0f)
            {
                const float bandFrac = (float) k / (float) numBins;
                const float w = bandFrac < 0.15f ? 1.4f    // kick band
                              : bandFrac > 0.60f ? 1.1f    // hats / snap
                                                 : 0.9f;
                flux += diff * w;
            }
            prevMag[(size_t) k] = mag;
        }
        if (! first) envelope.push_back (flux);
        first = false;
    }

    // Normalise to unit max so downstream thresholds/tempo are scale-invariant.
    float mx = 1.0e-9f;
    for (float v : envelope) mx = juce::jmax (mx, v);
    for (float& v : envelope) v /= mx;
    return envelope;
}

// Autocorrelation of the onset envelope, weighted by a log-Gaussian tempo prior
// (centred ~125 BPM) to bias toward the perceptually likely tempo, then an
// explicit octave-error guard that scores {bpm, 2x, 0.5x} by a comb over tempo
// multiples and keeps the strongest musically-sane candidate.
float estimateTempoFromOnsets (const std::vector<float>& env, double sampleRate, int hopSize)
{
    if (env.size() < 16 || sampleRate <= 0.0 || hopSize <= 0)
        return 0.0f;

    const double framesPerSec = sampleRate / (double) hopSize;
    auto bpmToLag = [&] (double bpm) { return juce::jmax (1, (int) std::round (framesPerSec * 60.0 / bpm)); };
    auto lagToBpm = [&] (int lag)    { return framesPerSec * 60.0 / (double) lag; };
    auto prior    = [] (double bpm)  { const double l = std::log2 (bpm / 125.0); return std::exp (-0.5 * l * l / (0.45 * 0.45)); };

    const int minLag = bpmToLag (210.0);
    const int maxLag = juce::jmin ((int) env.size() - 1, bpmToLag (55.0));
    if (maxLag <= minLag) return 0.0f;

    std::vector<double> ac ((size_t) (maxLag + 1), 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (size_t i = (size_t) lag; i < env.size(); ++i)
            s += (double) env[i] * (double) env[i - (size_t) lag];
        ac[(size_t) lag] = s;
    }

    double best = -1.0; int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double score = ac[(size_t) lag] * prior (lagToBpm (lag));
        if (score > best) { best = score; bestLag = lag; }
    }
    if (bestLag <= 0 || best <= 1.0e-9) return 0.0f;

    const double baseBpm = lagToBpm (bestLag);

    // Octave-error guard via per-beat SALIENCE: the true (kick-defined) tempo lands
    // its beats on the strong onsets, so its best-phase average onset energy per
    // beat is higher than the double tempo (which alternates kick/weak-offbeat).
    // Weighted by the perceptual prior to break ties (e.g. 174 DnB vs 87).
    auto salience = [&] (double bpm) -> double
    {
        const int P = juce::jmax (2, bpmToLag (bpm));
        const int step = juce::jmax (1, P / 8);
        double bestAvg = 0.0;
        for (int ph = 0; ph < P; ph += step)
        {
            double s = 0.0; int c = 0;
            for (int f = ph; f < (int) env.size(); f += P) { s += (double) env[(size_t) f]; ++c; }
            if (c > 0) bestAvg = juce::jmax (bestAvg, s / (double) c);
        }
        return bestAvg;
    };

    double bestBpm = baseBpm, bestScore = -1.0;
    for (double cand : { baseBpm, baseBpm * 2.0, baseBpm * 0.5 })
    {
        if (cand < 55.0 || cand > 210.0) continue;
        const double sc = salience (cand) * prior (cand);
        if (sc > bestScore) { bestScore = sc; bestBpm = cand; }
    }
    return juce::jlimit (40.0f, 240.0f, (float) bestBpm);
}

// Dynamic-programming beat tracker (Ellis 2007): find the beat sequence that
// maximises onset strength at beats minus a penalty for deviating from the
// target period. Produces beats that lock to real onsets while holding tempo —
// far better than a rigid evenly-spaced grid. Returns beat positions in samples.
std::vector<double> trackBeats (const std::vector<float>& env, double sampleRate, int hopSize, float bpm)
{
    std::vector<double> beats;
    const int N = (int) env.size();
    if (N < 8 || bpm <= 1.0f || sampleRate <= 0.0) return beats;

    const double framesPerBeat = (sampleRate / (double) hopSize) * 60.0 / (double) bpm;
    if (framesPerBeat < 2.0) return beats;

    const double tightness = 100.0;
    const int wmin = juce::jmax (1, (int) std::floor (framesPerBeat * 0.5));
    const int wmax = (int) std::ceil (framesPerBeat * 2.0);

    std::vector<double> score ((size_t) N, 0.0);
    std::vector<int>    back  ((size_t) N, -1);
    for (int t = 0; t < N; ++t)
    {
        double bestPrevScore = 0.0; int bestPrev = -1;
        const int lo = juce::jmax (0, t - wmax);
        const int hi = t - wmin;
        for (int p = lo; p <= hi; ++p)
        {
            const double err = std::log ((double) (t - p) / framesPerBeat);
            const double s   = score[(size_t) p] - tightness * err * err;
            if (s > bestPrevScore) { bestPrevScore = s; bestPrev = p; }
        }
        score[(size_t) t] = (double) env[(size_t) t] + bestPrevScore;
        back[(size_t) t]  = bestPrev;
    }

    // Backtrace from the strongest cumulative score.
    int t = 0; double mx = -1.0;
    for (int i = 0; i < N; ++i) if (score[(size_t) i] > mx) { mx = score[(size_t) i]; t = i; }
    while (t >= 0) { beats.push_back ((double) t * (double) hopSize); t = back[(size_t) t]; }
    std::reverse (beats.begin(), beats.end());
    return beats;
}

// Downbeat via 4/4 phase: group tracked beats by (index % 4), pick the phase with
// the greatest average onset strength (kick tends to fall on beat 1). Beats-based
// and structural — better than "first transient above threshold".
double detectDownbeat (const std::vector<double>& beats, const std::vector<float>& env,
                       int hopSize, double fallback)
{
    if (beats.size() < 4 || env.empty()) return fallback;
    std::array<double, 4> phaseEnergy { 0.0, 0.0, 0.0, 0.0 };
    std::array<int, 4>    phaseCount  { 0, 0, 0, 0 };
    for (size_t b = 0; b < beats.size(); ++b)
    {
        const int frame = (int) std::round (beats[b] / (double) hopSize);
        if (frame < 0 || frame >= (int) env.size()) continue;
        const int ph = (int) (b % 4);
        phaseEnergy[(size_t) ph] += (double) env[(size_t) frame];
        phaseCount [(size_t) ph] += 1;
    }
    int bestPhase = 0; double bestAvg = -1.0;
    for (int p = 0; p < 4; ++p)
    {
        const double avg = phaseCount[(size_t) p] > 0 ? phaseEnergy[(size_t) p] / phaseCount[(size_t) p] : 0.0;
        if (avg > bestAvg) { bestAvg = avg; bestPhase = p; }
    }
    return beats[(size_t) bestPhase];
}

// Local tempo curve: short-window autocorrelation of the onset envelope, searched
// within +/-40% of the global tempo (so it tracks accel/rit without octave jumps),
// then lightly smoothed. Flat for constant-tempo tracks.
std::vector<std::pair<double, float>> makeTempoCurve (const std::vector<float>& env, double sr, int hop, float globalBpm)
{
    std::vector<std::pair<double, float>> curve;
    const int N = (int) env.size();
    if (N < 16 || globalBpm <= 1.0f) return curve;

    const double framesPerSec = sr / (double) hop;
    const int winFrames = juce::jmax (8, (int) std::round (framesPerSec * 6.0));   // 6 s window
    const int hopFrames = juce::jmax (1, (int) std::round (framesPerSec * 1.0));   // 1 s step
    auto bpmToLag = [&] (double bpm) { return juce::jmax (1, (int) std::round (framesPerSec * 60.0 / bpm)); };
    auto lagToBpm = [&] (int lag)    { return framesPerSec * 60.0 / (double) lag; };
    const int minLag = bpmToLag ((double) globalBpm * 1.4);
    const int maxLag = bpmToLag ((double) globalBpm * 0.6);

    for (int c = 0; c + winFrames / 2 < N; c += hopFrames)
    {
        const int end = juce::jmin (N, c + winFrames);
        double best = -1.0; int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double s = 0.0;
            for (int i = c + lag; i < end; ++i) s += (double) env[(size_t) i] * (double) env[(size_t) (i - lag)];
            if (s > best) { best = s; bestLag = lag; }
        }
        if (bestLag > 0)
            curve.push_back ({ (double) (c + winFrames / 2) * (double) hop,
                               (float) juce::jlimit (40.0, 240.0, lagToBpm (bestLag)) });
    }
    // Light 3-tap smoothing.
    if (curve.size() >= 3)
    {
        std::vector<float> sm (curve.size());
        for (size_t i = 0; i < curve.size(); ++i)
        {
            float s = 0.0f; int c2 = 0;
            for (int k = -1; k <= 1; ++k) { const int j = (int) i + k; if (j >= 0 && j < (int) curve.size()) { s += curve[(size_t) j].second; ++c2; } }
            sm[i] = s / (float) c2;
        }
        for (size_t i = 0; i < curve.size(); ++i) curve[i].second = sm[i];
    }
    return curve;
}

// Dynamic-programming beat tracker with a TIME-VARYING target period taken from
// the tempo curve — so it tracks tempo drift instead of fighting a single global
// tempo. Falls back to the constant-period tracker when the curve is empty.
std::vector<double> trackBeatsVariable (const std::vector<float>& env, double sr, int hop,
                                        const std::vector<std::pair<double, float>>& curve, float globalBpm)
{
    const int N = (int) env.size();
    if (N < 8 || globalBpm <= 1.0f) return {};
    if (curve.size() < 2) return trackBeats (env, sr, hop, globalBpm);

    const double framesPerSec = sr / (double) hop;
    auto periodAt = [&] (int frame) -> double
    {
        const double pos = (double) frame * (double) hop;
        float bpm = globalBpm;
        if (pos <= curve.front().first)      bpm = curve.front().second;
        else if (pos >= curve.back().first)  bpm = curve.back().second;
        else for (size_t i = 1; i < curve.size(); ++i)
            if (curve[i].first >= pos)
            {
                const double t = (pos - curve[i - 1].first) / juce::jmax (1.0, curve[i].first - curve[i - 1].first);
                bpm = curve[i - 1].second + (float) t * (curve[i].second - curve[i - 1].second);
                break;
            }
        return framesPerSec * 60.0 / (double) juce::jmax (1.0f, bpm);
    };

    const double tightness = 100.0;
    std::vector<double> score ((size_t) N, 0.0);
    std::vector<int>    back  ((size_t) N, -1);
    for (int t = 0; t < N; ++t)
    {
        const double period = periodAt (t);
        const int wmin = juce::jmax (1, (int) std::floor (period * 0.5));
        const int wmax = (int) std::ceil (period * 2.0);
        double bestPrev = 0.0; int bestP = -1;
        for (int p = juce::jmax (0, t - wmax); p <= t - wmin; ++p)
        {
            const double err = std::log ((double) (t - p) / period);
            const double s   = score[(size_t) p] - tightness * err * err;
            if (s > bestPrev) { bestPrev = s; bestP = p; }
        }
        score[(size_t) t] = (double) env[(size_t) t] + bestPrev;
        back[(size_t) t]  = bestP;
    }
    int t = 0; double mx = -1.0;
    for (int i = 0; i < N; ++i) if (score[(size_t) i] > mx) { mx = score[(size_t) i]; t = i; }
    std::vector<double> beats;
    while (t >= 0) { beats.push_back ((double) t * (double) hop); t = back[(size_t) t]; }
    std::reverse (beats.begin(), beats.end());
    return beats;
}

// Meter + downbeat: test beats-per-bar in {4,3} and every bar phase, choosing the
// grouping whose downbeats carry the most onset energy relative to the other beats
// (kick-on-1). Returns beats-per-bar; writes the winning downbeat beat-index.
int detectMeter (const std::vector<double>& beats, const std::vector<float>& env, int hop, int& downbeatIndexOut)
{
    downbeatIndexOut = 0;
    if (beats.size() < 6 || env.empty()) return 4;

    std::vector<float> strength (beats.size(), 0.0f);
    for (size_t i = 0; i < beats.size(); ++i)
    {
        const int f = (int) std::round (beats[i] / (double) hop);
        if (f >= 0 && f < (int) env.size()) strength[i] = env[(size_t) f];
    }

    int bestB = 4, bestPhase = 0; double bestRatio = -1.0;
    for (int B : { 4, 3 })
        for (int p = 0; p < B; ++p)
        {
            double down = 0.0, other = 0.0; int dc = 0, oc = 0;
            for (size_t i = 0; i < strength.size(); ++i)
            {
                if ((int) (i % (size_t) B) == p) { down += strength[i]; ++dc; }
                else                             { other += strength[i]; ++oc; }
            }
            const double da = dc ? down / dc : 0.0;
            const double oa = oc ? other / oc : 1.0e-9;
            const double ratio = da / (oa + 1.0e-9);
            if (ratio > bestRatio + 1.0e-6) { bestRatio = ratio; bestB = B; bestPhase = p; }
        }
    downbeatIndexOut = bestPhase;
    return bestB;
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

    // Tempo curve (variable tempo) → DP beat tracker that follows drift → meter +
    // downbeat detection. Falls back to an even grid if tracking is thin.
    result.tempoCurve = makeTempoCurve (onsetEnvelope, sampleRate, onsetHopSize, result.detectedBpm);
    auto trackedBeats = trackBeatsVariable (onsetEnvelope, sampleRate, onsetHopSize, result.tempoCurve, result.detectedBpm);
    const double firstTransient = result.transientAnchorSamples.empty() ? 0.0 : result.transientAnchorSamples.front();
    if (trackedBeats.size() >= 6)
    {
        result.beatGridSamples = std::move (trackedBeats);
        int downbeatIndex = 0;
        result.beatsPerBar    = detectMeter (result.beatGridSamples, onsetEnvelope, onsetHopSize, downbeatIndex);
        result.downbeatSample = result.beatGridSamples[(size_t) juce::jlimit (0, (int) result.beatGridSamples.size() - 1, downbeatIndex)];
    }
    else
    {
        result.downbeatSample  = firstTransient;
        result.beatsPerBar     = 4;
        result.beatGridSamples = makeBeatGrid ((double) audio.getNumSamples(), sampleRate, result.detectedBpm, result.downbeatSample);
    }

    // ── Key detection ─────────────────────────────────────────────────────────
    // Whole-track chroma (not just the intro) with tuning compensation, then
    // Krumhansl–Schmuckl profile correlation across all 24 keys. Windows spread
    // across the entire track make the estimate robust to atypical intros and
    // breakdowns; estimating global tuning first keeps chroma sharp for records
    // that sit off A=440 (vinyl rips, live recordings, non-standard tunings).
    constexpr int windowSize = 4096;
    if (audio.getNumSamples() < windowSize)
        return result;

    std::vector<int> chromaStarts;
    {
        const int usable = audio.getNumSamples() - windowSize;
        constexpr int maxWindows = 96;
        const int n = juce::jmax (1, juce::jmin (maxWindows, usable / windowSize + 1));
        for (int k = 0; k < n; ++k)
            chromaStarts.push_back ((int) ((int64_t) usable * k / juce::jmax (1, n - 1)));
    }

    auto noteFreq = [] (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); };

    // Global tuning offset in [-0.5, +0.4] semitone (0.1 steps): the offset that
    // concentrates the most energy on note centres wins.
    double tuningOffset = 0.0;
    {
        const int tuneWindows = juce::jmin ((int) chromaStarts.size(), 40);
        double bestScore = -1.0;
        // Search strictly inside (-0.5, +0.5) semitone in 0.05 steps. We must NOT
        // hit ±0.5: that boundary aliases to the same frequency grid but shifts
        // every pitch-class label by a semitone (a +40c note gets captured by the
        // next semitone's probe), which would misdetect the key by a semitone.
        for (int step = -9; step <= 9; ++step)
        {
            const double off = 0.05 * (double) step;
            double score = 0.0;
            for (int wi = 0; wi < tuneWindows; ++wi)
                for (int midi = 60; midi <= 84; ++midi)   // upper register: cents == more Hz == resolvable
                {
                    const double f = noteFreq ((double) midi + off);
                    if (f >= sampleRate * 0.45) continue;
                    // Sum POWER (not amplitude): energy concentrates on the note
                    // grid only when the offset matches the track's tuning, so a
                    // squared measure discriminates the true offset far better.
                    score += (double) goertzelPower (audio, chromaStarts[(size_t) wi], windowSize, sampleRate, f);
                }
            if (score > bestScore) { bestScore = score; tuningOffset = off; }
        }
    }

    std::array<float, 12> chroma {};
    int windows = 0;
    for (int start : chromaStarts)
    {
        ++windows;
        for (int midi = 36; midi <= 84; ++midi)
        {
            const double frequency = noteFreq ((double) midi + tuningOffset);
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
