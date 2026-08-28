// Accuracy check for the upgraded beat/tempo analyzer: synthesize click tracks at
// known BPMs (kick on the beat, hats on the offbeat) and confirm TrackAnalyzer
// recovers the tempo, a matching beat-grid spacing, and a sane downbeat.
#include "TrackAnalysis.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

static juce::AudioBuffer<float> makeClickTrack (double bpm, double sr, double durSec)
{
    const int n = (int) (sr * durSec);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    const double beat = sr * 60.0 / bpm;

    // Kick: 55 Hz decaying sine on every beat.
    for (double t = 0.0; t < n; t += beat)
    {
        const int s = (int) t;
        for (int i = 0; i < (int) (sr * 0.10) && s + i < n; ++i)
        {
            const float env = std::exp (-(float) i / (float) (sr * 0.025));
            const float v = 0.9f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * (float) i / (float) sr) * env;
            b.addSample (0, s + i, v); b.addSample (1, s + i, v);
        }
    }
    // Hats: short noise burst on every offbeat.
    for (double t = beat * 0.5; t < n; t += beat)
    {
        const int s = (int) t;
        for (int i = 0; i < (int) (sr * 0.02) && s + i < n; ++i)
        {
            const float env = std::exp (-(float) i / (float) (sr * 0.004));
            const float v = 0.25f * (((float) (std::rand() % 1000) / 500.0f) - 1.0f) * env;
            b.addSample (0, s + i, v); b.addSample (1, s + i, v);
        }
    }
    return b;
}

// Accelerating track: instantaneous BPM ramps startBpm -> endBpm across the clip.
static juce::AudioBuffer<float> makeRampTrack (double startBpm, double endBpm, double sr, double durSec)
{
    const int n = (int) (sr * durSec);
    juce::AudioBuffer<float> b (2, n); b.clear();
    double t = 0.0;
    while (t < durSec)
    {
        const int s = (int) (t * sr);
        for (int i = 0; i < (int) (sr * 0.10) && s + i < n; ++i)
        {
            const float env = std::exp (-(float) i / (float) (sr * 0.025));
            const float v = 0.9f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * (float) i / (float) sr) * env;
            b.addSample (0, s + i, v); b.addSample (1, s + i, v);
        }
        const double bpm = startBpm + (endBpm - startBpm) * (t / durSec);
        t += 60.0 / bpm;
    }
    return b;
}

// 3/4 waltz: strong kick on beat 1 of every bar, weaker on 2 and 3.
static juce::AudioBuffer<float> makeWaltz (double bpm, double sr, double durSec)
{
    const int n = (int) (sr * durSec);
    juce::AudioBuffer<float> b (2, n); b.clear();
    const double beat = sr * 60.0 / bpm;
    int idx = 0;
    for (double tp = 0.0; tp < n; tp += beat, ++idx)
    {
        const int s = (int) tp;
        const float amp = (idx % 3 == 0) ? 1.0f : 0.4f;   // downbeat accent
        for (int i = 0; i < (int) (sr * 0.10) && s + i < n; ++i)
        {
            const float env = std::exp (-(float) i / (float) (sr * 0.025));
            const float v = amp * 0.9f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * (float) i / (float) sr) * env;
            b.addSample (0, s + i, v); b.addSample (1, s + i, v);
        }
    }
    return b;
}

int main (int argc, char** argv)
{
    const double sr = 44100.0;

    // Real-file mode: `StemDeckBeatDetectTest <audio-file>` prints our analysis of
    // a real track — for a head-to-head against djay Pro's reported BPM/beatgrid.
    if (argc >= 2)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        const juce::File f { juce::String (argv[1]) };
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) { std::cerr << "cannot read " << f.getFullPathName() << "\n"; return 2; }
        juce::AudioBuffer<float> buf ((int) rd->numChannels, (int) rd->lengthInSamples);
        rd->read (&buf, 0, (int) rd->lengthInSamples, 0, true, true);
        const auto a = TrackAnalyzer::analyze (buf, rd->sampleRate);
        double sp = 0.0; int c = 0;
        for (size_t i = 1; i < a.beatGridSamples.size(); ++i) { sp += a.beatGridSamples[i] - a.beatGridSamples[i - 1]; ++c; }
        const double gridBpm = c ? rd->sampleRate * 60.0 / (sp / c) : 0.0;
        std::cout << "file: " << f.getFileName() << "  (" << (double) rd->lengthInSamples / rd->sampleRate << "s)\n"
                  << "  detected BPM : " << juce::String (a.detectedBpm, 2) << "\n"
                  << "  beat-grid BPM: " << juce::String (gridBpm, 2) << "  (" << a.beatGridSamples.size() << " beats)\n"
                  << "  meter        : " << a.beatsPerBar << "/4\n"
                  << "  downbeat     : " << juce::String (a.downbeatSample / rd->sampleRate, 3) << " s\n"
                  << "  tempo curve  : " << (a.tempoCurve.empty() ? juce::String ("flat")
                        : juce::String (a.tempoCurve.front().second, 1) + " -> " + juce::String (a.tempoCurve.back().second, 1) + " BPM")
                  << "\n";
        return 0;
    }

    int fails = 0;
    for (double bpm : { 90.0, 120.0, 128.0, 140.0, 174.0 })
    {
        const auto buf = makeClickTrack (bpm, sr, 20.0);
        const auto a = TrackAnalyzer::analyze (buf, sr);

        double sumSpacing = 0.0; int cnt = 0;
        for (size_t i = 1; i < a.beatGridSamples.size(); ++i)
        { sumSpacing += a.beatGridSamples[i] - a.beatGridSamples[i - 1]; ++cnt; }
        const double gridBpm = cnt > 0 ? sr * 60.0 / (sumSpacing / cnt) : 0.0;

        const double tempoErr = std::abs (a.detectedBpm - bpm);
        const double gridErr  = std::abs (gridBpm - bpm);
        const bool ok = tempoErr <= 2.0 && gridErr <= 3.0;
        if (! ok) ++fails;
        std::cout << "true=" << bpm
                  << "  detected=" << juce::String (a.detectedBpm, 2)
                  << "  gridBpm=" << juce::String (gridBpm, 2)
                  << "  beats=" << a.beatGridSamples.size()
                  << "  downbeat@" << juce::String (a.downbeatSample / sr, 3) << "s"
                  << "  " << (ok ? "OK" : "FAIL") << "\n";
    }
    // Tempo curve: accelerating 120 -> 132.
    {
        const auto buf = makeRampTrack (120.0, 132.0, sr, 24.0);
        const auto a = TrackAnalyzer::analyze (buf, sr);
        const float startBpm = a.tempoCurve.empty() ? 0.0f : a.tempoCurve.front().second;
        const float endBpm   = a.tempoCurve.empty() ? 0.0f : a.tempoCurve.back().second;
        const bool ok = ! a.tempoCurve.empty() && std::abs (startBpm - 120.0f) <= 5.0f
                        && std::abs (endBpm - 132.0f) <= 5.0f && endBpm > startBpm + 4.0f;
        if (! ok) ++fails;
        std::cout << "ramp 120->132: curve " << juce::String (startBpm, 1) << " -> " << juce::String (endBpm, 1)
                  << " (" << a.tempoCurve.size() << " pts)  " << (ok ? "OK" : "FAIL") << "\n";
    }
    // Meter: 3/4 waltz should report beatsPerBar = 3.
    {
        const auto buf = makeWaltz (120.0, sr, 20.0);
        const auto a = TrackAnalyzer::analyze (buf, sr);
        const bool ok = a.beatsPerBar == 3;
        if (! ok) ++fails;
        std::cout << "waltz 3/4: detected beatsPerBar=" << a.beatsPerBar << "  " << (ok ? "OK" : "FAIL") << "\n";
    }

    std::cout << (fails == 0 ? "BEAT_DETECT_OK" : "BEAT_DETECT_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
