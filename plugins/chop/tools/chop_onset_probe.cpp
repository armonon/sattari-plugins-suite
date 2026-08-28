// Sattari Chop — onset detection probe on REAL material.
//
// The pure-std contract (core/tests/onset_curve_contract.cpp) proves the algorithm on
// signals with known onset positions. The Chop spec is blunt that this proves nothing
// about real use: "never claim 'verified' without testing on real, full-length, messy
// material". This probe is that test — it runs SattariOnsetCurve over actual full-length
// tracks (the GiantSteps corpus in validation/giantsteps/wav) and checks the properties
// that decide whether chopping is usable:
//
//   * onset density is musically plausible (not 3 markers, not 10k)
//   * sensitivity stays monotonic on real audio, not just synthetic
//   * detected onsets concentrate on transients (mean level at a marker is well above
//     the track's average) — i.e. markers are on hits, not scattered at random
//   * analyze() is fast enough that re-thresholding per drag frame is the right design
//
//   chop_onset_probe [numTracks]      # default 8
#include <sattari_audio_core/SattariOnsetCurve.h>

#include <JuceHeader.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
struct TrackResult
{
    juce::String name;
    double seconds = 0.0, analyzeMs = 0.0;
    int nLo = 0, nMid = 0, nHi = 0;
    double density = 0.0;       // markers/sec at mid sensitivity
    double onsetLevelRatio = 0.0; // peak near markers / overall mean level
    bool monotonic = false;
};

// Mean absolute level in a short window around a position.
float windowLevel (const std::vector<float>& x, std::int64_t pos, int half)
{
    const std::int64_t a = std::max<std::int64_t> (0, pos - half);
    const std::int64_t b = std::min<std::int64_t> ((std::int64_t) x.size(), pos + half);
    if (b <= a) return 0.0f;
    double s = 0.0;
    for (std::int64_t i = a; i < b; ++i) s += std::abs (x[(std::size_t) i]);
    return (float) (s / (double) (b - a));
}
}

int main (int argc, char** argv)
{
    const int want = (argc > 1) ? std::max (1, atoi (argv[1])) : 8;

    juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile ("validation/giantsteps/wav");
    if (! dir.isDirectory())
    {
        std::fprintf (stderr, "ERROR: %s not found (run from the repo root)\n", dir.getFullPathName().toRawUTF8());
        return 2;
    }

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
    files.sort();
    if (files.isEmpty()) { std::fprintf (stderr, "ERROR: no wavs in %s\n", dir.getFullPathName().toRawUTF8()); return 2; }

    juce::AudioFormatManager fm; fm.registerBasicFormats();

    std::vector<TrackResult> results;
    const int n = std::min (want, files.size());
    std::printf ("Sattari Chop onset probe — %d real track(s) from GiantSteps\n\n", n);

    for (int t = 0; t < n; ++t)
    {
        const auto f = files[t];
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr) { std::fprintf (stderr, "  skip (unreadable): %s\n", f.getFileName().toRawUTF8()); continue; }

        const std::int64_t len = (std::int64_t) reader->lengthInSamples;
        const double sr = reader->sampleRate;
        juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) len);
        reader->read (&buf, 0, (int) len, 0, true, true);

        // Mono sum — chopping works off the composite, as the user hears it.
        std::vector<float> mono ((std::size_t) len, 0.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const float* p = buf.getReadPointer (ch);
            for (std::int64_t i = 0; i < len; ++i) mono[(std::size_t) i] += p[i];
        }
        const float inv = 1.0f / (float) std::max (1, buf.getNumChannels());
        for (auto& v : mono) v *= inv;

        TrackResult r;
        r.name = f.getFileName();
        r.seconds = (double) len / sr;

        sattari::dsp::OnsetCurve oc;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        oc.analyze (mono.data(), len, sr);
        r.analyzeMs = juce::Time::getMillisecondCounterHiRes() - t0;

        const auto lo  = oc.candidates (0.10f);
        const auto mid = oc.candidates (0.50f);
        const auto hi  = oc.candidates (0.90f);
        r.nLo = (int) lo.size(); r.nMid = (int) mid.size(); r.nHi = (int) hi.size();
        r.density = r.seconds > 0.0 ? (double) r.nMid / r.seconds : 0.0;
        r.monotonic = (lo.size() <= mid.size() && mid.size() <= hi.size());

        // Is each marker on a RISE? Comparing marker level to the track mean is meaningless
        // on heavily-limited electronic music where everything sits near full scale. An
        // onset is by definition energy arriving, so measure level just AFTER vs just
        // BEFORE the marker. >1 means the detector found an attack; ~1 means it fired on
        // steady-state audio.
        const int w = (int) (sr * 0.010);   // 10 ms either side
        double rise = 0.0; int counted = 0;
        for (const auto& c : mid)
        {
            const float before = windowLevel (mono, c.samplePos - w, w / 2);
            const float after  = windowLevel (mono, c.samplePos + w / 2, w / 2);
            if (before > 1.0e-6f) { rise += (double) after / (double) before; ++counted; }
        }
        r.onsetLevelRatio = counted > 0 ? rise / (double) counted : 0.0;

        results.push_back (r);
        std::printf ("  %-16s %6.1fs  analyze %6.1fms  markers lo/mid/hi %4d/%4d/%4d  density %5.2f/s  rise x%.2f %s\n",
                     r.name.toRawUTF8(), r.seconds, r.analyzeMs, r.nLo, r.nMid, r.nHi,
                     r.density, r.onsetLevelRatio, r.monotonic ? "" : "  [NON-MONOTONIC]");
    }

    if (results.empty()) { std::fprintf (stderr, "ERROR: no tracks analysed\n"); return 2; }

    // ---- Aggregate judgements on REAL material -------------------------------
    int fails = 0;
    auto fail = [&fails] (const char* msg) { std::fprintf (stderr, "FAIL: %s\n", msg); ++fails; };

    double avgDensity = 0.0, avgRatio = 0.0, worstRatio = 1e9, avgMsPerSec = 0.0;
    int nonMono = 0, implausible = 0;
    for (const auto& r : results)
    {
        avgDensity += r.density;
        avgRatio   += r.onsetLevelRatio;
        worstRatio  = std::min (worstRatio, r.onsetLevelRatio);
        avgMsPerSec += r.seconds > 0 ? r.analyzeMs / r.seconds : 0.0;
        if (! r.monotonic) ++nonMono;
        // Electronic music: a few markers a second is sane. Wildly outside = unusable.
        if (r.density < 0.2 || r.density > 40.0) ++implausible;
    }
    const double k = (double) results.size();
    avgDensity /= k; avgRatio /= k; avgMsPerSec /= k;

    std::printf ("\n  mean density %.2f markers/s | mean rise x%.2f (worst x%.2f) | analyze %.1f ms per audio-second\n",
                 avgDensity, avgRatio, worstRatio, avgMsPerSec);

    if (nonMono > 0)          fail ("sensitivity is not monotonic on real audio");
    if (implausible > 0)      fail ("onset density implausible on real audio");
    if (avgRatio < 1.25)      fail ("markers are not landing on rises (level after marker barely exceeds level before)");
    if (worstRatio < 1.05)    fail ("at least one track put markers on non-rising audio");
    if (avgMsPerSec > 500.0)  fail ("analyze() too slow to be a one-shot pass");

    std::printf (fails == 0 ? "\nchop onset probe: PASS (real material)\n"
                            : "\nchop onset probe: %d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
