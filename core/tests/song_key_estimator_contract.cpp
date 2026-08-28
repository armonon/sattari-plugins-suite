#include <sattari_audio_core/SattariSongKeyEstimator.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace sattari::dsp;

namespace
{
int failures = 0;

void check (const char* label, bool condition)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", label);
        ++failures;
    }
}

double noteHz (int pitchClass, int octave = 4)
{
    const int midi = 12 * (octave + 1) + pitchClass;
    return 440.0 * std::pow (2.0, ((double) midi - 69.0) / 12.0);
}

std::vector<float> progression (int root, bool minor, double sampleRate)
{
    constexpr double chordSeconds = 1.25;
    const int chordSamples = (int) std::round (chordSeconds * sampleRate);
    const int roots[8] = {
        root, (root + 5) % 12, (root + 7) % 12, root,
        root, (root + 5) % 12, (root + 7) % 12, root
    };
    std::vector<float> audio ((size_t) chordSamples * 8, 0.0f);
    for (int chord = 0; chord < 8; ++chord)
    {
        const bool chordMinor = chord == 1 ? minor : (chord == 2 ? false : minor);
        const int third = chordMinor ? 3 : 4;
        const int notes[3] = {
            roots[chord],
            (roots[chord] + third) % 12,
            (roots[chord] + 7) % 12
        };
        for (int i = 0; i < chordSamples; ++i)
        {
            const double time = (double) i / sampleRate;
            const double edge = std::min (1.0, std::min ((double) i / 256.0,
                (double) (chordSamples - 1 - i) / 256.0));
            double sample = 0.0;
            for (int note : notes)
            {
                const double frequency = noteHz (note, note < 5 ? 4 : 3);
                sample += 0.20 * std::sin (2.0 * juce::MathConstants<double>::pi
                                           * frequency * time);
                sample += 0.05 * std::sin (4.0 * juce::MathConstants<double>::pi
                                           * frequency * time);
            }
            audio[(size_t) chord * (size_t) chordSamples + (size_t) i]
                = (float) (edge * sample);
        }
    }
    return audio;
}

KeyAnalysisResult analyseChunked (const std::vector<float>& audio,
                                  double sampleRate, int chunk)
{
    SongKeyEstimator estimator;
    estimator.prepare (sampleRate, (std::int64_t) audio.size());
    for (size_t position = 0; position < audio.size(); position += (size_t) chunk)
    {
        const int count = (int) std::min ((size_t) chunk, audio.size() - position);
        estimator.addSamples (audio.data() + position, count);
    }
    estimator.finalise();
    return estimator.result();
}
} // namespace

int main()
{
    for (double sampleRate : { 44100.0, 48000.0 })
    {
        for (int root = 0; root < 12; ++root)
        {
            for (bool minor : { false, true })
            {
                const auto audio = progression (root, minor, sampleRate);
                const auto result = analyseChunked (audio, sampleRate, 16384);
                check ("all 24 keys remain valid across 44.1/48 kHz",
                       result.valid && result.root == root && result.minor == minor);
            }
        }
    }

    const auto audio = progression (9, true, 44100.0);
    const auto whole = analyseChunked (audio, 44100.0, (int) audio.size());
    const auto small = analyseChunked (audio, 44100.0, 257);
    check ("file result is independent of decoder chunk size",
           whole.valid && small.valid
           && whole.root == small.root && whole.minor == small.minor
           && std::fabs (whole.confidence - small.confidence) < 1.0e-12);

    auto boundaryBass = progression (9, false, 44100.0);
    for (size_t i = 0; i < boundaryBass.size(); ++i)
        boundaryBass[i] += (float) (0.12 * std::sin (
            2.0 * juce::MathConstants<double>::pi * 27.5
            * (double) i / 44100.0));
    const auto boundaryResult = analyseChunked (boundaryBass, 44100.0, 4096);
    check ("25 Hz boundary sub-bass cannot pull the tonic to its dominant",
           boundaryResult.valid && boundaryResult.root == 9 && ! boundaryResult.minor);

    std::vector<float> silence (44100, 0.0f);
    check ("silence has no invented key",
           ! analyseChunked (silence, 44100.0, 4096).valid);

    if (failures == 0)
    {
        std::printf ("song key estimator contract: OK\n");
        return 0;
    }
    std::printf ("song key estimator contract: %d FAILURE(S)\n", failures);
    return 1;
}
