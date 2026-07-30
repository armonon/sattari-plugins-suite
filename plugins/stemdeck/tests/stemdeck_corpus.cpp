// Manifest-driven real-music accuracy gate.
//
// This executable deliberately contains no generated fixtures. It only passes
// when given a corpus manifest whose entries point at real, full-length audio
// and whose expected BPM/key/meter were established independently.
//
//   StemDeckCorpusGate path/to/corpus.json [report.json]
//
// WHAT IS GRADED
//
// The product's answer is not the DSP analyser's answer. MainPluginProcessor
// runs the DSP pass first, then lets the neural beat/key models override it —
// but only above their confidence floors. Grading TrackAnalyzer alone therefore
// certified the half of the stack nobody was worried about, and left the actual
// open defect (fixed-threshold peak-picking in NeuralBeatEngine) and the
// anti-alias pre-filter completely unmeasured.
//
// So this gate computes three results per track:
//
//   dsp        TrackAnalyzer::analyze — the first-pass answer a deck goes playable on
//   neural     NeuralBeatEngine + NeuralKeyEngine, when compiled in and installed
//   effective  what the app would actually use: neural where it clears the floor,
//              DSP otherwise — mirroring MainPluginProcessor::analyseInto
//
// PASS/FAIL is decided on `effective`. dsp and neural are reported alongside it,
// so the report answers "is the model earning its override?" — which is the
// question the confidence floors exist to settle and which no synthetic test can.
//
// REFUSING GENERATED FIXTURES
//
// A self-declared sourceType string is not evidence. Every file is profiled for
// high-frequency content and spectral variation, because this project's specific
// recurring failure is certifying on synthetic tones: a sine with an amplitude
// envelope has essentially no energy above 8 kHz and a near-static spectrum.
// The thresholds are deliberately permissive — they are meant to catch generated
// fixtures, not to adjudicate mastering — and the measured values go in the
// report so a false positive is diagnosable rather than mysterious.
//
// Manifest schema (v1):
// {
//   "version": 1,
//   "minimumTracks": 5,
//   "minimumMinutes": 20,
//   "tracks": [{
//     "file": "audio/song.wav",
//     "label": "artist - title",
//     "sourceType": "real_music",
//     "expectedBpm": 124.0,
//     "bpmTolerance": 2.0,
//     "expectedKeyPitchClass": 9,
//     "expectedKeyMinor": true,
//     "expectedMeter": 4,
//     "minimumDurationSeconds": 120
//   }]
// }

#include "TrackAnalysis.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#if SATTARI_STEMDECK_HAS_ONNX
 #include "NeuralBeatEngine.h"
 #include "NeuralKeyEngine.h"
#endif

namespace
{
// Mirrors MainPluginProcessor's floors. Duplicated rather than included because
// pulling in the processor would drag the whole plugin into this tool; if those
// constants move, this gate stops grading what actually ships.
constexpr float kNeuralKeyConfidenceFloor  = 0.45f;
constexpr float kNeuralBeatConfidenceFloor = 0.35f;

double number (const juce::DynamicObject& o, const juce::Identifier& key, double fallback)
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : (double) v;
}

int integer (const juce::DynamicObject& o, const juce::Identifier& key, int fallback)
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : (int) v;
}

bool boolean (const juce::DynamicObject& o, const juce::Identifier& key, bool fallback)
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : (bool) v;
}

juce::String text (const juce::DynamicObject& o, const juce::Identifier& key,
                   const juce::String& fallback = {})
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : v.toString();
}

// ── Generated-fixture detector ───────────────────────────────────────────────
struct SignalProfile
{
    double hfEnergyRatio = 0.0;   // energy above 8 kHz / total energy
    double spectralChange = 0.0;  // mean |delta log-magnitude| between frames
    bool   looksSynthetic = false;
    juce::String reason;
};

// Two independent tells, either of which is disqualifying.
//
// HF energy is the strong one. A real recording ALWAYS carries some energy above
// 8 kHz — hiss, room tone, cymbal bleed — even when it is dark or lo-fi. A
// digitally generated tone has mathematically none. Measured on this project's
// own fixture shapes: sine+envelope 0.0, music-shaped 3.6e-3, noise 0.64. The
// floor sits ~360x below the music-shaped case, so dull real material is safe.
//
// A frozen spectrum is the weaker one, kept for held tones and looped buffers
// that happen to carry HF. It is set very low so real music cannot reach it —
// the quietest thing measured here was 0.10 and typical music is >1.0.
//
// These were originally combined with AND, which let the repo's own fixture
// through: amplitude modulation gives a sine genuine frame-to-frame spectral
// change, so it cleared the movement test while having zero HF. The self-test
// caught that; see runSelfTest().
constexpr double kMinHfEnergyRatio  = 1.0e-5;
constexpr double kMinSpectralChange = 0.02;

SignalProfile profileSignal (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    SignalProfile p;
    constexpr int order = 11;                 // 2048-point frames
    constexpr int fftSize = 1 << order;
    const int channels = juce::jmax (1, audio.getNumChannels());
    const int n = audio.getNumSamples();
    if (n < fftSize * 4 || sampleRate <= 0.0)
        return p;                             // too short to judge; leave it alone

    juce::dsp::FFT fft (order);
    std::vector<float>  scratch ((size_t) fftSize * 2, 0.0f);
    std::vector<double> previous ((size_t) fftSize / 2, 0.0);
    std::vector<double> current  ((size_t) fftSize / 2, 0.0);

    // Sample up to 200 frames spread across the WHOLE file rather than a prefix —
    // an intro tells you very little about a track.
    const int maxFrames = 200;
    const int usableFrames = juce::jmax (1, (n - fftSize) / fftSize);
    const int step = juce::jmax (1, usableFrames / maxFrames);

    const int hfBin = (int) std::ceil (8000.0 / (sampleRate / (double) fftSize));
    double hfSum = 0.0, totalSum = 0.0, changeSum = 0.0;
    int frames = 0, changeFrames = 0;

    for (int f = 0; f * step < usableFrames; ++f)
    {
        const int start = f * step * fftSize;
        if (start + fftSize > n) break;

        std::fill (scratch.begin(), scratch.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
        {
            double s = 0.0;
            for (int c = 0; c < channels; ++c) s += audio.getSample (c, start + i);
            // Hann window, so a pure tone does not smear across the spectrum and
            // fake broadband content.
            const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (fftSize - 1));
            scratch[(size_t) i] = (float) (s / channels * w);
        }

        fft.performFrequencyOnlyForwardTransform (scratch.data());

        double frameTotal = 0.0, frameHf = 0.0, framePeak = 0.0;
        for (int b = 1; b < fftSize / 2; ++b)
        {
            const double mag = (double) scratch[(size_t) b];
            const double e = mag * mag;
            frameTotal += e;
            if (b >= hfBin) frameHf += e;
            framePeak = juce::jmax (framePeak, mag);
            current[(size_t) b] = mag;
        }

        if (frameTotal > 1.0e-12)
        {
            hfSum += frameHf;
            totalSum += frameTotal;

            if (frames > 0)
            {
                // Only compare bins carrying real content. Taking log10 of every
                // bin measured the numerical floor of the EMPTY ones instead of
                // the signal: a 220 Hz sine scored 0.25 on this, higher than some
                // music, purely from noise in bins that hold nothing.
                const double floorMag = juce::jmax (framePeak, 1.0e-9) * 1.0e-3;   // -60 dB
                double d = 0.0;
                int counted = 0;
                for (int b = 1; b < fftSize / 2; ++b)
                {
                    const double a = current[(size_t) b], p = previous[(size_t) b];
                    if (juce::jmax (a, p) < floorMag) continue;
                    d += std::abs (std::log10 (a + 1.0e-12) - std::log10 (p + 1.0e-12));
                    ++counted;
                }
                if (counted > 0) { changeSum += d / (double) counted; ++changeFrames; }
            }
            previous = current;
            ++frames;
        }
    }

    if (frames == 0) return p;
    p.hfEnergyRatio  = totalSum > 0.0 ? hfSum / totalSum : 0.0;
    p.spectralChange = changeFrames > 0 ? changeSum / changeFrames : 0.0;

    const bool noTopOctave    = p.hfEnergyRatio  < kMinHfEnergyRatio;
    const bool frozenSpectrum = p.spectralChange < kMinSpectralChange;
    if (noTopOctave || frozenSpectrum)
    {
        p.looksSynthetic = true;
        p.reason = noTopOctave
            ? "no energy above 8 kHz: hfEnergyRatio=" + juce::String (p.hfEnergyRatio, 9)
                + " < " + juce::String (kMinHfEnergyRatio, 9)
            : "spectrum does not move: spectralChange=" + juce::String (p.spectralChange, 4)
                + " < " + juce::String (kMinSpectralChange, 4);
    }
    return p;
}

// ── Grading ──────────────────────────────────────────────────────────────────
struct Expectation
{
    double bpm = -1.0, bpmTolerance = 2.0;
    int    keyPitchClass = -1;
    bool   keyMinor = false;
    int    meter = 0;
};

struct Grade
{
    bool bpmGraded = false, bpmPass = false;
    bool keyGraded = false, keyPass = false;
    bool meterGraded = false, meterPass = false;
    int  bpmOctave = 0;   // 0 = none; else the ratio that fit (2, -2, 3, -3)

    bool allGradedPass() const noexcept
    {
        return (! bpmGraded   || bpmPass)
            && (! keyGraded   || keyPass)
            && (! meterGraded || meterPass);
    }
    int gradedDimensions() const noexcept
    {
        return (bpmGraded ? 1 : 0) + (keyGraded ? 1 : 0) + (meterGraded ? 1 : 0);
    }
};

// Half/double (and third/triple) tempo is THE classic beat-tracker failure. It is
// still a failure here, but "octave-confused on 4 of 5" and "lost on 4 of 5" are
// different problems and the report has to be able to tell them apart.
int octaveRelation (double detected, double expected, double tolerance)
{
    if (detected <= 0.0 || expected <= 0.0) return 0;
    const std::pair<double, int> ratios[] {
        { 2.0, 2 }, { 0.5, -2 }, { 3.0, 3 }, { 1.0 / 3.0, -3 } };
    for (const auto& r : ratios)
        if (std::abs (detected - expected * r.first) <= tolerance * r.first)
            return r.second;
    return 0;
}

Grade grade (double bpm, int keyPitchClass, bool keyMinor, int meter, const Expectation& want)
{
    Grade g;
    if (want.bpm > 0.0)
    {
        g.bpmGraded = true;
        g.bpmPass = std::abs (bpm - want.bpm) <= want.bpmTolerance;
        if (! g.bpmPass) g.bpmOctave = octaveRelation (bpm, want.bpm, want.bpmTolerance);
    }
    if (want.keyPitchClass >= 0)
    {
        g.keyGraded = true;
        g.keyPass = keyPitchClass == want.keyPitchClass && keyMinor == want.keyMinor;
    }
    if (want.meter > 0)
    {
        g.meterGraded = true;
        g.meterPass = meter == want.meter;
    }
    return g;
}

void addGrade (juce::DynamicObject& o, const juce::String& prefix, const Grade& g)
{
    o.setProperty (prefix + "BpmGraded", g.bpmGraded);
    o.setProperty (prefix + "BpmPass", g.bpmPass);
    o.setProperty (prefix + "BpmOctaveError", g.bpmOctave);
    o.setProperty (prefix + "KeyGraded", g.keyGraded);
    o.setProperty (prefix + "KeyPass", g.keyPass);
    o.setProperty (prefix + "MeterGraded", g.meterGraded);
    o.setProperty (prefix + "MeterPass", g.meterPass);
    o.setProperty (prefix + "Pass", g.allGradedPass());
}

// ── Self-test for the fixture detector ───────────────────────────────────────
// The detector is the part of this gate that carries a CLAIM ("generated audio
// is refused"), so it has to be falsifiable. This mode builds the exact kind of
// signal the repo's other tests use as fixtures and asserts it is caught, plus
// broadband material that must NOT be caught.
//
// This validates only the synthetic side of the threshold. Whether real music
// ever trips it cannot be known until a real corpus exists — which is the point
// of the gate — so the report always carries the measured values.
int runSelfTest()
{
    constexpr double sr = 44100.0;
    constexpr int seconds = 20;
    const int n = (int) (sr * seconds);
    juce::Random rng (20260728);
    int failures = 0;

    auto report = [&failures] (const char* name, const SignalProfile& p, bool wantSynthetic)
    {
        const bool ok = p.looksSynthetic == wantSynthetic;
        if (! ok) ++failures;
        std::cout << (ok ? "  ok   " : "  FAIL ") << name
                  << " looksSynthetic=" << (p.looksSynthetic ? "true" : "false")
                  << " (want " << (wantSynthetic ? "true" : "false") << ")"
                  << "  hf=" << juce::String (p.hfEnergyRatio, 8)
                  << "  change=" << juce::String (p.spectralChange, 4) << "\n";
    };

    std::cout << "fixture-detector self-test\n";

    // 1. The repo's own fixture shape: a tone under a slow amplitude envelope.
    {
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const float env = 0.35f + 0.45f * (float) std::abs (std::sin (t * juce::MathConstants<double>::twoPi));
            const float s = env * (float) std::sin (t * 220.0 * juce::MathConstants<double>::twoPi);
            b.setSample (0, i, s); b.setSample (1, i, s);
        }
        report ("sine + amplitude envelope (a generated fixture)", profileSignal (b, sr), true);
    }

    // 2. Broadband noise — nothing like music, but it has a live top octave and a
    //    moving spectrum, so the detector must NOT reject it. This is the guard
    //    against a detector so blunt it fails anything unusual.
    {
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float s = rng.nextFloat() * 2.0f - 1.0f;
            b.setSample (0, i, s * 0.3f); b.setSample (1, i, s * 0.3f);
        }
        report ("broadband noise", profileSignal (b, sr), false);
    }

    // 3. Something music-shaped: bass + harmonics + noisy transients on a grid.
    {
        juce::AudioBuffer<float> b (2, n);
        const double beat = 60.0 / 124.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            double s = 0.35 * std::sin (t * 55.0 * juce::MathConstants<double>::twoPi);
            s += 0.18 * std::sin (t * 220.0 * juce::MathConstants<double>::twoPi);
            s += 0.10 * std::sin (t * 660.0 * juce::MathConstants<double>::twoPi);
            const double phase = std::fmod (t, beat) / beat;
            if (phase < 0.04)   // hat/snare transient: broadband, decaying
                s += 0.5 * (rng.nextDouble() * 2.0 - 1.0) * std::exp (-phase * 90.0);
            b.setSample (0, i, (float) s * 0.5f);
            b.setSample (1, i, (float) s * 0.5f);
        }
        report ("music-shaped (bass + harmonics + transients)", profileSignal (b, sr), false);
    }

    std::cout << (failures == 0 ? "CORPUS_SELFTEST_OK\n" : "CORPUS_SELFTEST_FAIL\n");
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 2 && juce::String (argv[1]) == "--selftest")
        return runSelfTest();

    if (argc < 2)
    {
        std::cerr << "usage: StemDeckCorpusGate corpus.json [report.json]\n"
                     "       StemDeckCorpusGate --selftest\n";
        return 2;
    }

    const juce::File manifest (juce::String::fromUTF8 (argv[1]));
    if (! manifest.existsAsFile())
    {
        std::cerr << "corpus manifest does not exist: " << manifest.getFullPathName() << "\n";
        return 2;
    }

    const auto root = juce::JSON::parse (manifest);
    auto* obj = root.getDynamicObject();
    if (obj == nullptr || integer (*obj, "version", 0) != 1)
    {
        std::cerr << "corpus manifest must be a version-1 JSON object\n";
        return 2;
    }

    const auto tracks = obj->getProperty ("tracks");
    auto* entries = tracks.getArray();
    if (entries == nullptr)
    {
        std::cerr << "corpus manifest has no tracks array\n";
        return 2;
    }

    const int minimumTracks = integer (*obj, "minimumTracks", 5);
    const double minimumMinutes = number (*obj, "minimumMinutes", 20.0);

    // ── Neural engines: load once, reuse across the corpus ───────────────────
    bool neuralBeatReady = false, neuralKeyReady = false;
#if SATTARI_STEMDECK_HAS_ONNX
    NeuralBeatEngine beatEngine;
    NeuralKeyEngine  keyEngine;
    neuralBeatReady = beatEngine.loadDefaultModel() && beatEngine.isAvailable();
    neuralKeyReady  = keyEngine.loadDefaultModel()  && keyEngine.isAvailable();
#endif
    std::cout << "neural beat model: " << (neuralBeatReady ? "loaded" : "UNAVAILABLE")
              << " | neural key model: " << (neuralKeyReady ? "loaded" : "UNAVAILABLE") << "\n";

    int failures = 0;
    int realTracks = 0;
    int gradedDimensionTotal = 0;
    int dspStood = 0, neuralOverrides = 0;
    double totalSeconds = 0.0;
    juce::Array<juce::var> results;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (const auto& entry : *entries)
    {
        auto* e = entry.getDynamicObject();
        if (e == nullptr)
        {
            ++failures;
            std::cerr << "FAIL: corpus entry is not an object\n";
            continue;
        }

        const auto relative = text (*e, "file");
        const auto file = juce::File::isAbsolutePath (relative)
                        ? juce::File (relative)
                        : manifest.getParentDirectory().getChildFile (relative);
        const auto label = text (*e, "label", file.getFileNameWithoutExtension());
        const bool declaredReal = text (*e, "sourceType") == "real_music";
        if (! declaredReal)
        {
            ++failures;
            std::cerr << "FAIL: " << label << " is not truth-labelled sourceType=real_music\n";
            continue;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 1 || reader->sampleRate <= 0.0)
        {
            ++failures;
            std::cerr << "FAIL: cannot decode " << file.getFullPathName() << "\n";
            continue;
        }
        if (reader->lengthInSamples > (juce::int64) std::numeric_limits<int>::max())
        {
            ++failures;
            std::cerr << "FAIL: track is too long for the current analysis buffer: " << label << "\n";
            continue;
        }

        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        if (! reader->read (&audio, 0, audio.getNumSamples(), 0, true, true))
        {
            ++failures;
            std::cerr << "FAIL: decode failed for " << label << "\n";
            continue;
        }

        const double sr = reader->sampleRate;
        const double duration = (double) reader->lengthInSamples / sr;

        // The declared label says what it is SUPPOSED to be; this measures what it is.
        const auto profile = profileSignal (audio, sr);
        if (profile.looksSynthetic)
        {
            ++failures;
            std::cerr << "FAIL: " << label << " is declared real_music but measures as generated ("
                      << profile.reason << ")\n";
            continue;
        }

        Expectation want;
        want.bpm           = number (*e, "expectedBpm", -1.0);
        want.bpmTolerance  = number (*e, "bpmTolerance", 2.0);
        want.keyPitchClass = integer (*e, "expectedKeyPitchClass", -1);
        want.keyMinor      = boolean (*e, "expectedKeyMinor", false);
        want.meter         = integer (*e, "expectedMeter", 0);
        const double minimumDuration = number (*e, "minimumDurationSeconds", 120.0);

        // ── The three results ────────────────────────────────────────────────
        const auto dsp = TrackAnalyzer::analyze (audio, sr);

        double effBpm   = dsp.detectedBpm;
        int    effKey   = dsp.keyPitchClass;
        bool   effMinor = dsp.keyIsMinor;
        int    effMeter = dsp.beatsPerBar;
        bool   beatOverridden = false, keyOverridden = false;
        double neuralBpm = 0.0;  float neuralBeatConf = 0.0f;  int neuralMeter = 0;
        int    neuralKeyRoot = -1; bool neuralKeyMinor = false; float neuralKeyConf = 0.0f;

#if SATTARI_STEMDECK_HAS_ONNX
        if (neuralBeatReady)
        {
            const auto b = beatEngine.detect (audio, sr);
            if (b.valid)
            {
                neuralBpm = b.detectedBpm;
                neuralBeatConf = b.confidence;
                neuralMeter = b.beatsPerBar;
                // Identical predicate to MainPluginProcessor::analyseInto.
                if (b.confidence >= kNeuralBeatConfidenceFloor && b.beatGridSamples.size() >= 4)
                {
                    beatOverridden = true;
                    effMeter = b.beatsPerBar;
                    if (b.detectedBpm > 1.0f) effBpm = b.detectedBpm;
                }
            }
        }
        if (neuralKeyReady)
        {
            const auto k = keyEngine.detect (audio, sr);
            if (k.valid)
            {
                neuralKeyRoot = k.root;
                neuralKeyMinor = k.minor;
                neuralKeyConf = k.confidence;
                if (k.confidence >= kNeuralKeyConfidenceFloor)
                {
                    keyOverridden = true;
                    effKey = k.root;
                    effMinor = k.minor;
                }
            }
        }
#endif
        if (beatOverridden || keyOverridden) ++neuralOverrides; else ++dspStood;

        const auto dspGrade = grade (dsp.detectedBpm, dsp.keyPitchClass, dsp.keyIsMinor,
                                     dsp.beatsPerBar, want);
        const auto effGrade = grade (effBpm, effKey, effMinor, effMeter, want);
        Grade neuGrade;
        if (neuralBpm > 0.0 || neuralKeyRoot >= 0)
            neuGrade = grade (neuralBpm > 0.0 ? neuralBpm : dsp.detectedBpm,
                              neuralKeyRoot >= 0 ? neuralKeyRoot : dsp.keyPitchClass,
                              neuralKeyRoot >= 0 ? neuralKeyMinor : dsp.keyIsMinor,
                              neuralMeter > 0 ? neuralMeter : dsp.beatsPerBar, want);

        const bool durationOk = duration >= minimumDuration;
        const bool pass = effGrade.allGradedPass() && durationOk;
        failures += pass ? 0 : 1;
        ++realTracks;
        gradedDimensionTotal += effGrade.gradedDimensions();
        totalSeconds += duration;

        auto* r = new juce::DynamicObject();
        r->setProperty ("label", label);
        r->setProperty ("file", file.getFullPathName());
        r->setProperty ("durationSeconds", duration);
        r->setProperty ("durationPass", durationOk);
        r->setProperty ("hfEnergyRatio", profile.hfEnergyRatio);
        r->setProperty ("spectralChange", profile.spectralChange);
        r->setProperty ("dspBpm", dsp.detectedBpm);
        r->setProperty ("dspKeyPitchClass", dsp.keyPitchClass);
        r->setProperty ("dspKeyMinor", dsp.keyIsMinor);
        r->setProperty ("dspMeter", dsp.beatsPerBar);
        addGrade (*r, "dsp", dspGrade);
        r->setProperty ("neuralBeatAvailable", neuralBeatReady);
        r->setProperty ("neuralKeyAvailable", neuralKeyReady);
        r->setProperty ("neuralBpm", neuralBpm);
        r->setProperty ("neuralBeatConfidence", neuralBeatConf);
        r->setProperty ("neuralKeyPitchClass", neuralKeyRoot);
        r->setProperty ("neuralKeyMinor", neuralKeyMinor);
        r->setProperty ("neuralKeyConfidence", neuralKeyConf);
        addGrade (*r, "neural", neuGrade);
        r->setProperty ("beatOverriddenByNeural", beatOverridden);
        r->setProperty ("keyOverriddenByNeural", keyOverridden);
        r->setProperty ("effectiveBpm", effBpm);
        r->setProperty ("effectiveKeyPitchClass", effKey);
        r->setProperty ("effectiveKeyMinor", effMinor);
        r->setProperty ("effectiveMeter", effMeter);
        r->setProperty ("gradedDimensions", effGrade.gradedDimensions());
        addGrade (*r, "effective", effGrade);
        r->setProperty ("pass", pass);
        results.add (juce::var (r));

        std::cout << (pass ? "PASS " : "FAIL ") << label
                  << "  eff bpm=" << juce::String (effBpm, 2)
                  << " key=" << TrackAnalyzer::pitchClassName (effKey) << (effMinor ? "m" : "")
                  << " meter=" << effMeter << "/4"
                  << " [" << (beatOverridden ? "neural-beat" : "dsp-beat")
                  << "/" << (keyOverridden ? "neural-key" : "dsp-key") << "]"
                  << "  dsp bpm=" << juce::String (dsp.detectedBpm, 2);
        if (neuralBpm > 0.0)
            std::cout << "  neural bpm=" << juce::String (neuralBpm, 2)
                      << " (conf " << juce::String (neuralBeatConf, 2) << ")";
        if (effGrade.bpmOctave != 0)
            std::cout << "  [OCTAVE ERROR x" << effGrade.bpmOctave << "]";
        std::cout << "  duration=" << juce::String (duration, 1) << "s\n";
    }

    if (realTracks < minimumTracks)
    {
        ++failures;
        std::cerr << "FAIL: corpus has " << realTracks << " real tracks; requires "
                  << minimumTracks << "\n";
    }
    if (totalSeconds < minimumMinutes * 60.0)
    {
        ++failures;
        std::cerr << "FAIL: corpus has " << juce::String (totalSeconds / 60.0, 1)
                  << " minutes; requires " << juce::String (minimumMinutes, 1) << "\n";
    }
    // A corpus that only ever specified expectedBpm still printed REAL_CORPUS_OK,
    // which reads as "accuracy certified" when only tempo was ever checked.
    if (realTracks > 0 && gradedDimensionTotal < realTracks * 2)
    {
        ++failures;
        std::cerr << "FAIL: corpus grades only " << gradedDimensionTotal
                  << " dimensions across " << realTracks
                  << " tracks; require at least two of bpm/key/meter per track\n";
    }

    auto* report = new juce::DynamicObject();
    report->setProperty ("schema", 2);
    report->setProperty ("manifest", manifest.getFullPathName());
    report->setProperty ("realTracks", realTracks);
    report->setProperty ("totalMinutes", totalSeconds / 60.0);
    report->setProperty ("gradedDimensions", gradedDimensionTotal);
    report->setProperty ("neuralBeatAvailable", neuralBeatReady);
    report->setProperty ("neuralKeyAvailable", neuralKeyReady);
    report->setProperty ("tracksWhereNeuralOverrode", neuralOverrides);
    report->setProperty ("tracksWhereDspStood", dspStood);
    report->setProperty ("failures", failures);
    report->setProperty ("pass", failures == 0);
    report->setProperty ("results", juce::var (results));
    const auto reportJson = juce::JSON::toString (juce::var (report), true);

    if (argc >= 3)
    {
        const juce::File reportFile (juce::String::fromUTF8 (argv[2]));
        reportFile.getParentDirectory().createDirectory();
        if (! reportFile.replaceWithText (reportJson))
        {
            std::cerr << "could not write report: " << reportFile.getFullPathName() << "\n";
            return 2;
        }
    }

    std::cout << (failures == 0 ? "REAL_CORPUS_OK" : "REAL_CORPUS_FAIL")
              << " tracks=" << realTracks
              << " minutes=" << juce::String (totalSeconds / 60.0, 1)
              << " neuralOverrode=" << neuralOverrides
              << " dspStood=" << dspStood
              << " failures=" << failures << "\n";
    if (! neuralBeatReady || ! neuralKeyReady)
        std::cout << "NOTE: a neural model was unavailable, so this run did NOT "
                     "certify the neural path.\n";
    return failures == 0 ? 0 : 1;
}
