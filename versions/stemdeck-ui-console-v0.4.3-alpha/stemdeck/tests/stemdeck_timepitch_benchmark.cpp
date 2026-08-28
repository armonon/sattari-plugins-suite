#include "DeckPlayer.h"
#include "TimePitchEngine.h"

#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double sourceSeconds = 8.0;
constexpr int renderSeconds = 4;

struct Metrics
{
    double rms = 0.0;
    double peak = 0.0;
    double maxAdjacentDelta = 0.0;
    double positiveZeroCrossingsPerSecond = 0.0;
    int nonFiniteSamples = 0;
};

[[noreturn]] void fail (const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit (1);
}

std::shared_ptr<const DeckPlayer::LoadedAudio> makeSyntheticDjLoop()
{
    auto loaded = std::make_shared<DeckPlayer::LoadedAudio>();
    const int samples = (int) std::round (sampleRate * sourceSeconds);
    loaded->audio.setSize (2, samples);
    loaded->sourceSampleRate = sampleRate;
    loaded->path = "synthetic://stemdeck-timepitch-benchmark-loop.wav";
    loaded->displayName = "StemDeck synthetic time/pitch benchmark loop";

    const double bpm = 124.0;
    const double beatSeconds = 60.0 / bpm;

    for (int i = 0; i < samples; ++i)
    {
        const double t = (double) i / sampleRate;
        const double beatPosition = std::fmod (t, beatSeconds) / beatSeconds;
        const double barBeat = std::fmod (t / beatSeconds, 4.0);

        const double kick = 0.9 * std::exp (-beatPosition * 42.0) * std::sin (juce::MathConstants<double>::twoPi * 54.0 * t);
        const double hatPhase = std::fmod (t, beatSeconds * 0.5) / (beatSeconds * 0.5);
        const double hat = 0.08 * std::exp (-hatPhase * 100.0) * std::sin (juce::MathConstants<double>::twoPi * 7600.0 * t);
        const double snare = (barBeat > 0.98 && barBeat < 1.18) || (barBeat > 2.98 && barBeat < 3.18)
                             ? 0.18 * std::sin (juce::MathConstants<double>::twoPi * 185.0 * t) * std::exp (-(barBeat - std::floor (barBeat)) * 18.0)
                             : 0.0;
        const double bass = 0.18 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                          + 0.06 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * t);
        const double chord = 0.08 * std::sin (juce::MathConstants<double>::twoPi * 261.625565 * t)
                           + 0.07 * std::sin (juce::MathConstants<double>::twoPi * 329.627557 * t)
                           + 0.06 * std::sin (juce::MathConstants<double>::twoPi * 391.995436 * t);
        const double vocalLike = 0.10 * std::sin (juce::MathConstants<double>::twoPi * (350.0 + 40.0 * std::sin (juce::MathConstants<double>::twoPi * 0.8 * t)) * t)
                               * (0.65 + 0.35 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * t));

        const float left = (float) juce::jlimit (-0.95, 0.95, kick + hat + snare + bass + chord + vocalLike);
        const float right = (float) juce::jlimit (-0.95, 0.95, kick + 0.8 * hat + snare + bass + 0.92 * chord + 0.85 * vocalLike);
        loaded->audio.setSample (0, i, left);
        loaded->audio.setSample (1, i, right);
    }

    loaded->analysis = TrackAnalyzer::analyze (loaded->audio, loaded->sourceSampleRate);
    return loaded;
}

juce::AudioBuffer<float> renderCase (const TimePitchDecision& decision, int blockSize)
{
    DeckPlayer deck;
    deck.prepare (sampleRate, blockSize, 2);
    deck.setLoadedAudio (makeSyntheticDjLoop());

    std::array<float, DeckPlayer::numStemLanes> stemGains {};
    stemGains.fill (-60.0f);
    stemGains[(size_t) DeckPlayer::fullMixStem] = -3.0f;

    const int totalSamples = (int) sampleRate * renderSeconds;
    juce::AudioBuffer<float> output (2, totalSamples);
    output.clear();

    for (int pos = 0; pos < totalSamples; pos += blockSize)
    {
        const int n = juce::jmin (blockSize, totalSamples - pos);
        deck.process (output, pos, n, true, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, decision, stemGains, {}, {});
    }

    return output;
}

Metrics measure (const juce::AudioBuffer<float>& buffer)
{
    Metrics metrics;
    double sumSquares = 0.0;
    int count = 0;
    int positiveCrossings = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        float previous = data[0];
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float current = data[i];
            if (! std::isfinite (current))
            {
                ++metrics.nonFiniteSamples;
                continue;
            }

            metrics.peak = juce::jmax (metrics.peak, std::abs ((double) current));
            sumSquares += (double) current * (double) current;
            ++count;

            if (i > 0)
            {
                metrics.maxAdjacentDelta = juce::jmax (metrics.maxAdjacentDelta, std::abs ((double) current - (double) previous));
                if (previous <= 0.0f && current > 0.0f)
                    ++positiveCrossings;
            }
            previous = current;
        }
    }

    metrics.rms = count > 0 ? std::sqrt (sumSquares / (double) count) : 0.0;
    metrics.positiveZeroCrossingsPerSecond = (double) positiveCrossings / ((double) buffer.getNumChannels() * (double) buffer.getNumSamples() / sampleRate);
    return metrics;
}

void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer)
{
    file.deleteFile();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (! stream)
        fail ("could not create WAV output " + file.getFullPathName().toStdString());

    auto options = juce::AudioFormatWriterOptions {}
                     .withSampleRate (sampleRate)
                     .withNumChannels (buffer.getNumChannels())
                     .withBitsPerSample (24);
    auto writer = format.createWriterFor (stream, options);
    if (! writer)
        fail ("could not create WAV writer " + file.getFullPathName().toStdString());
    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        fail ("could not write WAV " + file.getFullPathName().toStdString());
}

juce::String jsonForCase (const juce::String& name, const TimePitchDecision& decision, const Metrics& metrics, const juce::File& file)
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("name", name);
    object->setProperty ("file", file.getFullPathName());
    object->setProperty ("statusLabel", decision.statusLabel);
    object->setProperty ("tempoRatio", decision.tempoRatio);
    object->setProperty ("pitchSemitones", decision.pitchSemitones);
    object->setProperty ("pitchPreservingActive", decision.pitchPreservingActive);
    object->setProperty ("usingResampleFallback", decision.usingResampleFallback);
    object->setProperty ("rms", metrics.rms);
    object->setProperty ("peak", metrics.peak);
    object->setProperty ("maxAdjacentDelta", metrics.maxAdjacentDelta);
    object->setProperty ("positiveZeroCrossingsPerSecond", metrics.positiveZeroCrossingsPerSecond);
    object->setProperty ("nonFiniteSamples", metrics.nonFiniteSamples);
    return juce::JSON::toString (juce::var (object), true);
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::File outputDir = juce::File::getCurrentWorkingDirectory()
                             .getChildFile ("build")
                             .getChildFile ("stemdeck")
                             .getChildFile ("timepitch-benchmark");
    if (argc > 1)
        outputDir = juce::File (argv[1]);

    outputDir.createDirectory();

#if SATTARI_STEMDECK_HAS_RUBBERBAND
    const juce::String backend = "rubberband";
#else
    const juce::String backend = "granular";
#endif

    struct Case
    {
        juce::String name;
        TimePitchRequest request;
    };

    const std::vector<Case> cases {
        { "preserve_tempo_085", { 0.85f, 0.0f, true } },
        { "preserve_tempo_115", { 1.15f, 0.0f, true } },
        { "preserve_tempo_050_stress", { 0.5f, 0.0f, true } },
        { "preserve_tempo_200_stress", { 2.0f, 0.0f, true } },
        { "preserve_pitch_plus_03", { 1.0f, 3.0f, true } },
        { "preserve_pitch_minus_03", { 1.0f, -3.0f, true } },
        { "preserve_pitch_plus_12_stress", { 1.0f, 12.0f, true } },
        { "resample_tempo_115_reference", { 1.15f, 0.0f, false } }
    };

    juce::Array<juce::var> manifestCases;
    std::map<std::string, Metrics> measured;

    for (const auto& testCase : cases)
    {
        const auto decision = TimePitchEngine::decide (testCase.request);
        const auto rendered = renderCase (decision, 512);
        const auto metrics = measure (rendered);
        const auto file = outputDir.getChildFile (testCase.name + ".wav");
        writeWav (file, rendered);

        if (metrics.nonFiniteSamples != 0)
            fail (testCase.name.toStdString() + " produced non-finite samples");
        if (metrics.rms < 0.01)
            fail (testCase.name.toStdString() + " rendered near silence");
        if (metrics.peak > 1.01)
            fail (testCase.name.toStdString() + " exceeded safe peak");
        if (metrics.maxAdjacentDelta > 1.75)
            fail (testCase.name.toStdString() + " has a suspicious adjacent-sample jump");

        measured[testCase.name.toStdString()] = metrics;
        manifestCases.add (juce::JSON::parse (jsonForCase (testCase.name, decision, metrics, file)));
        std::cout << testCase.name << " backend=" << backend
                  << " status='" << decision.statusLabel << "'"
                  << " rms=" << metrics.rms
                  << " peak=" << metrics.peak
                  << " max_delta=" << metrics.maxAdjacentDelta
                  << " zc_per_sec=" << metrics.positiveZeroCrossingsPerSecond << '\n';
    }

    if (measured["resample_tempo_115_reference"].positiveZeroCrossingsPerSecond <= measured["preserve_tempo_115"].positiveZeroCrossingsPerSecond * 1.05)
        fail ("resample tempo reference did not pitch upward relative to key-lock tempo render");
    if (measured["preserve_pitch_plus_12_stress"].positiveZeroCrossingsPerSecond <= measured["preserve_pitch_plus_03"].positiveZeroCrossingsPerSecond * 1.35)
        fail ("+12 semitone stress render did not raise spectral/crossing energy enough relative to +3 semitone render");

    auto* manifest = new juce::DynamicObject();
    manifest->setProperty ("schema", "SattariStemDeck.timePitchBenchmark.v1");
    manifest->setProperty ("truthLabel", "internal benchmark/listening pack; not sale-ready evidence by itself");
    manifest->setProperty ("backend", backend);
    manifest->setProperty ("sampleRate", sampleRate);
    manifest->setProperty ("renderSeconds", renderSeconds);
    manifest->setProperty ("cases", manifestCases);
    outputDir.getChildFile ("manifest.json").replaceWithText (juce::JSON::toString (juce::var (manifest), true), false, false, "\n");

    std::cout << "StemDeck time/pitch benchmark ok\n"
              << "  backend=" << backend << '\n'
              << "  output=" << outputDir.getFullPathName() << '\n';
    return 0;
}
