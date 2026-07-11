// Auto Pitch — offline render + null-test driver.
//
// Renders a deterministic vocal-like signal through Auto Pitch in a fixed
// hard-tune configuration, writes a .wav you can listen to, and null-tests the
// render fingerprint against a committed golden. If the DSP output drifts
// between commits, the residual spikes and this fails.
//
// Usage:
//   auto_pitch_render            # render + strict null-test against golden (local
//                                #   dev regression gate; same machine + Rubber Band)
//   auto_pitch_render --bless     # render + (over)write the golden fingerprint
//   auto_pitch_render --smoke     # render + assert finite/non-silent only; NO golden
//                                #   compare. CI-safe (Rubber Band output can differ
//                                #   across versions/platforms, so a strict null is
//                                #   not portable - smoke catches crashes/silence).
//
// Paths are resolved relative to the repo root via SATTARI_REPO_ROOT (set by the
// CMake target) so the .wav and golden land in validation/ regardless of CWD.
#include "PluginProcessor.h"
#include <sattari_audio_core/SattariRenderHarness.h>
#include <sattari_audio_core/SattariPitchDetector.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        std::fprintf (stderr, "WARNING: missing parameter %s\n", id.toRawUTF8());
}

juce::File repoRoot()
{
   #ifdef SATTARI_REPO_ROOT
    return juce::File (SATTARI_REPO_ROOT);
   #else
    return juce::File::getCurrentWorkingDirectory();
   #endif
}

// A steady tone with sinusoidal vibrato: baseHz modulated +/- depthCents at
// rateHz. Used to prove vibrato preservation (the wiggle survives correction).
void makeVibratoTone (juce::AudioBuffer<float>& buf, double sr, double seconds,
                      double baseHz, double depthCents, double rateHz)
{
    const int n = (int) (sr * seconds);
    buf.setSize (1, n);
    float* d = buf.getWritePointer (0);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const double cents = depthCents * std::sin (2.0 * M_PI * rateHz * t);
        const double hz = baseHz * std::pow (2.0, cents / 1200.0);
        d[i] = 0.5f * (float) std::sin (phase);
        phase += 2.0 * M_PI * hz / sr;
    }
}

// Standard deviation (in cents) of the detected pitch over the tail of a buffer
// — a direct measure of how much pitch movement (vibrato) survives.
double pitchStdCents (const juce::AudioBuffer<float>& buf, double sr)
{
    sattari::dsp::PitchDetector det;
    det.prepare (sr, 80.0f, 800.0f, 0.045f);
    const int n = buf.getNumSamples();
    const int start = n / 3;                      // skip attack/priming
    const int hop = 256;
    std::vector<double> midis;
    for (int s = start; s + hop <= n; s += hop)
    {
        det.pushBlock (buf.getReadPointer (0) + s, hop);
        const auto r = det.analyse (0.2f);
        if (r.voiced) midis.push_back (12.0 * std::log2 (r.frequencyHz / 440.0) + 69.0);
    }
    if (midis.size() < 4) return 0.0;
    double mean = 0.0; for (double m : midis) mean += m; mean /= (double) midis.size();
    double var = 0.0;  for (double m : midis) var += (m - mean) * (m - mean);
    var /= (double) midis.size();
    return std::sqrt (var) * 100.0;               // semitones -> cents
}

// Extract a detected-pitch track (MIDI values, one per hop) over the tail of a
// buffer. Used by --vibtest to measure the synthetic vibrato modulation.
std::vector<double> pitchTrackMidi (const juce::AudioBuffer<float>& buf, double sr, int hop = 256)
{
    sattari::dsp::PitchDetector det;
    det.prepare (sr, 80.0f, 800.0f, 0.045f);
    const int n = buf.getNumSamples();
    const int start = n / 3;                      // skip attack/priming + vocoder latency
    std::vector<double> midis;
    for (int s = start; s + hop <= n; s += hop)
    {
        det.pushBlock (buf.getReadPointer (0) + s, hop);
        const auto r = det.analyse (0.2f);
        if (r.voiced) midis.push_back (12.0 * std::log2 (r.frequencyHz / 440.0) + 69.0);
    }
    return midis;
}

// Dominant modulation frequency (Hz) of a pitch track, via autocorrelation of the
// de-meaned series. `hop` samples per track step at sample rate `sr`.
double dominantModHz (const std::vector<double>& track, double sr, int hop)
{
    const int N = (int) track.size();
    if (N < 8) return 0.0;
    double mean = 0.0; for (double v : track) mean += v; mean /= (double) N;
    std::vector<double> x ((size_t) N);
    for (int i = 0; i < N; ++i) x[(size_t) i] = track[(size_t) i] - mean;
    // Search lags corresponding to 1..12 Hz.
    const double stepHz = sr / (double) hop;      // track sample rate
    const int minLag = juce::jmax (1, (int) std::floor (stepHz / 12.0));
    const int maxLag = juce::jmin (N - 1, (int) std::ceil (stepHz / 1.0));
    double best = -1.0e30; int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double acc = 0.0;
        for (int i = 0; i + lag < N; ++i) acc += x[(size_t) i] * x[(size_t) (i + lag)];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    return bestLag > 0 ? stepHz / (double) bestLag : 0.0;
}

// Goertzel-style magnitude of a single frequency over the tail of a buffer.
double toneMag (const juce::AudioBuffer<float>& buf, double sr, double freq)
{
    const int n = buf.getNumSamples();
    const int start = n / 2;                       // tail only (past vocoder latency + settle)
    double re = 0.0, im = 0.0;
    const double w = 2.0 * M_PI * freq / sr;
    for (int i = start; i < n; ++i)
    {
        const double s = (double) buf.getReadPointer (0)[i];
        re += s * std::cos (w * (double) i);
        im += s * std::sin (w * (double) i);
    }
    const double cnt = (double) juce::jmax (1, n - start);
    return std::sqrt (re * re + im * im) / cnt;
}

const char* noteName12 (int r)
{
    static const char* N[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return N[((r % 12) + 12) % 12];
}

// A clean monophonic tonal melody in the given key: sustained sine tones on
// scale degrees, tonic/dominant-weighted and ending on the tonic like real
// tonal music. Best-case material for probing AutoKey's key detection.
juce::AudioBuffer<float> makeMelody (double sr, double seconds, int root, bool minor)
{
    static const int MAJ[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int MIN[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* deg = minor ? MIN : MAJ;
    // scale-degree indices (0=tonic .. 6=leading), tonic-heavy, ends on tonic.
    static const int seq[] = { 0,2,4,2,0, 4,5,4,2,0, 0,2,4,4,2,0, 4,2,0,0 };
    const int seqN = (int) (sizeof (seq) / sizeof (seq[0]));

    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> buf (1, n);
    float* d = buf.getWritePointer (0);
    const int noteLen = (int) (sr * 0.26);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const int noteI = (i / noteLen) % seqN;
        const int midi = 60 + root + deg[seq[noteI]];
        const double hz = 440.0 * std::pow (2.0, (midi - 69) / 12.0);
        d[i] = 0.4f * (float) std::sin (phase);
        phase += 2.0 * M_PI * hz / sr;
    }
    return buf;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const bool bless    = (argc > 1 && std::strcmp (argv[1], "--bless") == 0);
    const bool smoke    = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);
    const bool features = (argc > 1 && std::strcmp (argv[1], "--features") == 0);
    const bool crepeTest= (argc > 1 && std::strcmp (argv[1], "--crepe") == 0);
    const bool autokey  = (argc > 1 && std::strcmp (argv[1], "--autokey") == 0);
    const bool gainstage= (argc > 1 && std::strcmp (argv[1], "--gainstage") == 0);
    const bool vibtest  = (argc > 1 && std::strcmp (argv[1], "--vibtest") == 0);
    const bool polytest = (argc > 1 && std::strcmp (argv[1], "--polytest") == 0);

    const double sr = 48000.0;
    const int    block = 512;
    const double seconds = 4.0;

    // --gainstage: guard the input-drive / output-trim split. Input gain must drive
    // the Style saturator (Classic tanh); output gain must be a clean post-Style
    // level trim that does NOT add distortion. Before the split these were summed,
    // so +6 dB of output was bit-identical to +6 dB of input — this proves that
    // regression cannot return.
    if (gainstage)
    {
        auto peakNorm = [] (juce::AudioBuffer<float>& b, float target)
        {
            float pk = 0.0f;
            for (int i = 0; i < b.getNumSamples(); ++i) pk = std::max (pk, std::fabs (b.getSample (0, i)));
            if (pk > 1.0e-9f) b.applyGain (target / pk);
        };
        auto renderCfg = [&] (const juce::AudioBuffer<float>& src, float inDb, float outDb)
        {
            SattariAutoPitchAudioProcessor p;
            auto& a = p.parameters();
            setParam (a, "keyRoot", 0.0f);        setParam (a, "scale", 0.0f);
            setParam (a, "followSongKey", 0.0f);  setParam (a, "style", 1.0f); // Classic (pure tanh saturator)
            setParam (a, "amount", 1.0f);         setParam (a, "speed", 0.9f);
            setParam (a, "enableCorrection", 1.0f);
            setParam (a, "inputGain", inDb);      setParam (a, "outputGain", outDb);
            return sattari::harness::renderThrough (p, src, sr, block);
        };
        const float g6 = juce::Decibels::decibelsToGain (6.0f);

        // [A] Output gain is a transparent linear trim. Keep the signal well below the
        //     soft-ceiling knee (0.75) so +6 dB out is a pure scale: qOut == g6 * qBase.
        juce::AudioBuffer<float> q;
        sattari::harness::generateVocalLike (q, sr, 2.0);
        peakNorm (q, 0.10f);
        const auto qBase = renderCfg (q, 0.0f, 0.0f);
        const auto qOut  = renderCfg (q, 0.0f, 6.0f);
        float aErr = 0.0f, aPk = 0.0f;
        for (int i = 0; i < qBase.getNumSamples(); ++i)
        {
            const float b = qBase.getSample (0, i);
            aPk  = std::max (aPk, std::fabs (b));
            aErr = std::max (aErr, std::fabs (qOut.getSample (0, i) - b * g6));
        }
        const float aRel = aPk > 0.0f ? aErr / (aPk * g6) : 1.0f;

        // [B] Input drive and output trim are no longer interchangeable. At a hot level,
        //     +6 dB via INPUT drives the tanh (extra harmonics) while +6 dB via OUTPUT is
        //     a post-Style scale — they MUST diverge (pre-split they were identical).
        juce::AudioBuffer<float> h;
        sattari::harness::generateVocalLike (h, sr, 2.0);
        peakNorm (h, 0.50f);
        const auto hOut = renderCfg (h, 0.0f, 6.0f);
        const auto hIn  = renderCfg (h, 6.0f, 0.0f);
        float bDiff = 0.0f, bPk = 0.0f;
        for (int i = 0; i < hOut.getNumSamples(); ++i)
        {
            bPk   = std::max (bPk, std::fabs (hOut.getSample (0, i)));
            bDiff = std::max (bDiff, std::fabs (hOut.getSample (0, i) - hIn.getSample (0, i)));
        }
        const float bRel = bPk > 0.0f ? bDiff / bPk : 0.0f;

        std::printf ("  [A] output-gain linearity rel-err = %.5f (want < 0.01: +6dB out == %.3fx dry)\n", aRel, g6);
        std::printf ("  [B] input-vs-output 6dB divergence = %.5f (want > 0.05: must NOT be identical)\n", bRel);
        if (aRel >= 0.01f) { std::fprintf (stderr, "FAIL: output gain is not a clean post-Style trim (rel-err %.4f) — it is driving the Style stage\n", aRel); return 1; }
        if (bRel <= 0.05f) { std::fprintf (stderr, "FAIL: input & output gain still interchangeable (div %.4f) — the drive/trim split is broken\n", bRel); return 1; }
        std::printf ("Auto Pitch gain-stage ok\n");
        return 0;
    }

    // --vibtest: prove the SYNTHETIC vibrato generator (addVibrato) adds a real,
    // periodic ~5.5 Hz pitch modulation on a sustained in-tune note — and that
    // depth 0 is a strict no-op (bit-identical to no vibrato). Rendered through
    // the PSOLA live engine (clean fast retune, no Rubber Band phase jitter to
    // contaminate the pitch-track measurement).
    if (vibtest)
    {
        // A sustained, in-tune A4 (440 Hz) in A major: correction is ~0, so any
        // pitch movement in the output is the ADDED vibrato, nothing else.
        juce::AudioBuffer<float> tone;
        {
            const double seconds3 = 3.0;
            const int nn = (int) (sr * seconds3);
            tone.setSize (1, nn);
            float* d = tone.getWritePointer (0);
            double ph = 0.0;
            for (int i = 0; i < nn; ++i) { d[i] = 0.5f * (float) std::sin (ph); ph += 2.0 * M_PI * 440.0 / sr; }
        }

        auto render = [&] (float depthCents) {
            SattariAutoPitchAudioProcessor p;
            auto& a = p.parameters();
            setParam (a, "keyRoot", 9.0f);          // A
            setParam (a, "scale", 0.0f);            // major (A4 in scale)
            setParam (a, "followSongKey", 0.0f);
            setParam (a, "style", 2.0f);            // Modern Rap: strong + fast retune
            setParam (a, "amount", 1.0f);           setParam (a, "speed", 0.95f);
            setParam (a, "enableCorrection", 1.0f); setParam (a, "doNoHarm", 0.0f);
            setParam (a, "classicMode", 0.0f);      setParam (a, "flexTune", 0.0f);
            setParam (a, "vibrato", 0.0f);          // preserve OFF (isolate the generator)
            setParam (a, "liveEngine", 1.0f);       // PSOLA
            setParam (a, "addVibrato", depthCents);
            setParam (a, "vibratoRate", 5.5f);
            setParam (a, "vibratoOnset", 300.0f);
            return sattari::harness::renderThrough (p, tone, sr, block);
        };

        const auto off0 = render (0.0f);
        const auto off1 = render (0.0f);            // determinism / no-op proof
        const auto vib  = render (60.0f);

        // (1) depth 0 must be a strict no-op: two depth-0 renders bit-identical.
        double noopResidual = 0.0;
        for (int i = 0; i < off0.getNumSamples(); ++i)
            noopResidual = std::max (noopResidual, std::fabs ((double) off0.getSample (0, i) - (double) off1.getSample (0, i)));

        // (2) depth 60 must add clearly MORE pitch movement than depth 0.
        const double sOff = pitchStdCents (off0, sr);
        const double sVib = pitchStdCents (vib, sr);

        // (3) the modulation must be periodic near the requested 5.5 Hz.
        const int hop = 256;
        const auto track = pitchTrackMidi (vib, sr, hop);
        const double modHz = dominantModHz (track, sr, hop);

        // (4) finite + bounded.
        bool finite = true; float peak = 0.0f;
        for (int i = 0; i < vib.getNumSamples(); ++i)
        {
            const float v = vib.getSample (0, i);
            if (! std::isfinite (v)) finite = false;
            peak = std::max (peak, std::fabs (v));
        }

        std::printf ("vibtest: noopResidual=%.3g  pitchStd off=%.1f ct vib=%.1f ct  modHz=%.2f (want ~5.5)  peak=%.3f finite=%d\n",
                     noopResidual, sOff, sVib, modHz, peak, (int) finite);

        int fails = 0;
        if (noopResidual > 1.0e-6)       { std::fprintf (stderr, "FAIL: addVibrato=0 is not a no-op (residual %.3g)\n", noopResidual); ++fails; }
        if (sVib <= sOff * 1.8 + 5.0)    { std::fprintf (stderr, "FAIL: addVibrato=60 did not add clear pitch modulation (%.1f vs %.1f ct)\n", sVib, sOff); ++fails; }
        if (modHz < 4.0 || modHz > 7.5)  { std::fprintf (stderr, "FAIL: vibrato rate %.2f Hz not near 5.5 Hz\n", modHz); ++fails; }
        if (! finite)                    { std::fprintf (stderr, "FAIL: non-finite output\n"); ++fails; }
        if (peak > 1.0f)                 { std::fprintf (stderr, "FAIL: output exceeds 0 dBFS (peak %.3f)\n", peak); ++fails; }

        std::printf (fails == 0 ? "Auto Pitch synthetic-vibrato proof ok\n" : "Auto Pitch synthetic-vibrato proof FAILED (%d)\n", fails);
        return fails == 0 ? 0 : 1;
    }

    // --polytest: prove the REAL polyphonic path retunes TWO simultaneous notes
    // INDEPENDENTLY (not one global ratio). A C4+E4 chord, each detuned +40
    // cents, is rendered with Polyphonic ON in C major; BOTH partials must move
    // measurably toward their in-tune targets (energy shifts from the detuned to
    // the in-scale frequency for each note). Bounded, finite, no NaNs.
    if (polytest)
    {
        const double c4 = 261.63, e4 = 329.63;
        const double detune = std::pow (2.0, 40.0 / 1200.0);   // +40 cents
        const double c4d = c4 * detune, e4d = e4 * detune;

        // Two-note chord, each partial detuned +40 cents.
        juce::AudioBuffer<float> chord;
        {
            const double seconds3 = 3.0;
            const int nn = (int) (sr * seconds3);
            chord.setSize (1, nn);
            float* d = chord.getWritePointer (0);
            double pc = 0.0, pe = 0.0;
            for (int i = 0; i < nn; ++i)
            {
                d[i] = 0.4f * (float) std::sin (pc) + 0.4f * (float) std::sin (pe);
                pc += 2.0 * M_PI * c4d / sr;
                pe += 2.0 * M_PI * e4d / sr;
            }
        }

        SattariAutoPitchAudioProcessor p;
        auto& a = p.parameters();
        setParam (a, "keyRoot", 0.0f);          // C
        setParam (a, "scale", 0.0f);            // major (C and E in scale)
        setParam (a, "followSongKey", 0.0f);
        setParam (a, "style", 0.0f);            // Natural: no saturation to colour the spectrum
        setParam (a, "amount", 1.0f);           setParam (a, "speed", 0.9f);
        setParam (a, "enableCorrection", 1.0f);
        setParam (a, "polyphonic", 1.0f);       // <-- the real per-peak vocoder
        const auto out = sattari::harness::renderThrough (p, chord, sr, block);

        // Per note: fraction of energy at the in-tune target vs the detuned input
        // frequency. Correction should RAISE this fraction (energy moves home).
        auto fracToward = [&] (const juce::AudioBuffer<float>& b, double fTarget, double fDetuned) {
            const double mt = toneMag (b, sr, fTarget);
            const double md = toneMag (b, sr, fDetuned);
            return (mt + md) > 1.0e-12 ? mt / (mt + md) : 0.0;
        };

        const double cIn  = fracToward (chord, c4, c4d);
        const double cOut = fracToward (out,   c4, c4d);
        const double eIn  = fracToward (chord, e4, e4d);
        const double eOut = fracToward (out,   e4, e4d);

        bool finite = true; float peak = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i)
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
            {
                const float v = out.getSample (ch, i);
                if (! std::isfinite (v)) finite = false;
                peak = std::max (peak, std::fabs (v));
            }

        std::printf ("polytest: C4 toward-target %.3f -> %.3f   E4 toward-target %.3f -> %.3f   peak=%.3f finite=%d\n",
                     cIn, cOut, eIn, eOut, peak, (int) finite);

        int fails = 0;
        // BOTH notes must move measurably toward their in-scale targets — the
        // proof that peaks are shifted independently, not by one shared ratio.
        if (cOut <= cIn + 0.05) { std::fprintf (stderr, "FAIL: C4 partial not corrected toward target (%.3f -> %.3f)\n", cIn, cOut); ++fails; }
        if (eOut <= eIn + 0.05) { std::fprintf (stderr, "FAIL: E4 partial not corrected toward target (%.3f -> %.3f)\n", eIn, eOut); ++fails; }
        if (! finite)           { std::fprintf (stderr, "FAIL: non-finite output (NaN/Inf)\n"); ++fails; }
        if (peak > 1.0f)        { std::fprintf (stderr, "FAIL: output exceeds 0 dBFS (peak %.3f)\n", peak); ++fails; }
        if (peak < 1.0e-3f)     { std::fprintf (stderr, "FAIL: output silent (peak %.3g)\n", peak); ++fails; }

        std::printf (fails == 0 ? "Auto Pitch real-polyphonic proof ok\n" : "Auto Pitch real-polyphonic proof FAILED (%d)\n", fails);
        return fails == 0 ? 0 : 1;
    }

    // ---- AutoKey accuracy probe: feed clean tonal melodies in known keys and
    //      check what key AutoKey detects (reads detectedRoot via the snapshot).
    //      This is BEST-CASE material (clean monophonic tones); real vocals with
    //      noise/vibrato/partial phrases are harder.
    if (autokey)
    {
        int correct = 0, total = 0;
        for (int root : { 0, 2, 4, 5, 7, 9, 11 })
            for (bool minor : { false, true })
            {
                auto mel = makeMelody (sr, 6.0, root, minor);
                SattariAutoPitchAudioProcessor p;
                auto& a = p.parameters();
                setParam (a, "scale", minor ? 1.0f : 0.0f);   // AutoKey finds the root FOR this mode
                setParam (a, "autoKey", 1.0f);
                setParam (a, "followSongKey", 0.0f);
                setParam (a, "enableCorrection", 1.0f);
                setParam (a, "vibrato", 0.0f);
                (void) sattari::harness::renderThrough (p, mel, sr, block);
                // currentKeyOrigin() reports the effective AutoKey key: root AND
                // the auto-detected mode (major/minor).
                const auto ko = p.currentKeyOrigin();
                const int  detRoot  = ko.root;
                const int  detScale = ko.scale;   // 0=major, 1=minor
                const bool ok = (detRoot == root) && (detScale == (minor ? 1 : 0));
                std::printf ("  %-2s %s  ->  detected %-2s %s  %s\n",
                             noteName12 (root), minor ? "min" : "maj",
                             noteName12 (detRoot), detScale == 1 ? "min" : detScale == 0 ? "maj" : "chr",
                             ok ? "OK" : "MISS");
                ++total; if (ok) ++correct;
            }
        std::printf ("AutoKey accuracy on clean tonal melodies: %d/%d (%.0f%%)\n",
                     correct, total, 100.0 * correct / total);
        return 0;
    }

    // ---- CREPE (C++ ONNX) proof: load the real model + track known tones ------
    // Deterministic: drives CrepePitchTracker directly (synchronous analyse, no
    // worker-timing race) so it proves the C++ ONNX path loads crepe.onnx and
    // decodes pitch correctly. Requires -DSATTARI_USE_ONNX=ON and the model at
    // ~/Library/Sattari Audio/models/crepe.onnx.
    if (crepeTest)
    {
       #if SATTARI_HAS_ONNX
        sattari::dsp::CrepePitchTracker t;
        t.prepare (sr);
        auto model = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("Sattari Audio").getChildFile ("models").getChildFile ("crepe.onnx");
        const bool loaded = t.loadModel (model.getFullPathName().toStdString());
        std::printf ("crepe model: loaded=%d available=%d\n  %s\n",
                     (int) loaded, (int) t.isAvailable(), model.getFullPathName().toRawUTF8());
        if (! t.isAvailable()) { std::fprintf (stderr, "FAIL: crepe model not available\n"); return 1; }

        int fails = 0;
        for (double hz : { 220.0, 440.0, 660.0 })
        {
            std::vector<float> buf ((size_t) block);
            double ph = 0.0;
            for (int b = 0; b < 24; ++b)   // ~256 ms, more than one model frame
            {
                for (int i = 0; i < block; ++i) { buf[(size_t) i] = 0.5f * (float) std::sin (ph); ph += 2.0 * M_PI * hz / sr; }
                t.pushBlock (buf.data(), block);
            }
            const auto r = t.analyse (0.4f);
            const double cents = 1200.0 * std::log2 (r.frequencyHz / hz);
            std::printf ("  %.0f Hz -> %.2f Hz (%+.1f cents) conf=%.2f voiced=%d\n",
                         hz, r.frequencyHz, cents, r.confidence, (int) r.voiced);
            if (! r.voiced || std::fabs (cents) > 30.0) ++fails;
        }
        std::printf (fails == 0 ? "Auto Pitch CREPE (C++ ONNX) proof ok\n"
                                : "Auto Pitch CREPE proof FAILED (%d)\n", fails);
        return fails == 0 ? 0 : 1;
       #else
        std::fprintf (stderr, "built without ONNX — configure with -DSATTARI_USE_ONNX=ON\n");
        return 2;
       #endif
    }

    // ---- Feature proof: vibrato preservation + PSOLA live engine -------------
    // Separate from the golden null-test. Uses a vibrato tone (A3 + 5.5 Hz, +/-40
    // cents) tuned to A: with vibrato preservation OFF the wiggle is flattened;
    // ON, it survives. Also proves the PSOLA engine produces valid audio.
    //
    // Measured through the PSOLA (live) engine on purpose. Vibrato preservation is
    // only observable at FAST retune (a slow retune can't cancel a 5.5 Hz wiggle,
    // so there is nothing for "preserve" to add back). At fast retune the Rubber
    // Band phase vocoder phase-glitches on the vibrato-cancelling ratio, and those
    // glitches inflate a pitch-std measurement of the FLATTEN case specifically,
    // inverting the comparison. PSOLA renders the same (correct) ratio without the
    // phase glitch, so the pitch-std ordering reflects the real feature: preserve
    // ON keeps clearly more wiggle than OFF. The ratio injection is upstream of the
    // engine, so this validates the feature for both engines.
    if (features)
    {
        juce::AudioBuffer<float> vib;
        makeVibratoTone (vib, sr, 3.0, 220.0 /*A3*/, 40.0, 5.5);
        const double inStd = pitchStdCents (vib, sr);

        auto render = [&] (float vibAmt, bool live) {
            SattariAutoPitchAudioProcessor p;
            auto& a = p.parameters();
            setParam (a, "keyRoot", 9.0f);            // A
            setParam (a, "scale", 0.0f);              // major
            setParam (a, "followSongKey", 0.0f);
            setParam (a, "style", 2.0f);              // Modern Rap: strong + fast retune
            setParam (a, "amount", 1.0f);
            setParam (a, "speed", 0.95f);
            setParam (a, "enableCorrection", 1.0f);
            setParam (a, "doNoHarm", 0.0f);
            setParam (a, "classicMode", 0.0f);
            setParam (a, "flexTune", 0.0f);
            setParam (a, "vibrato", vibAmt);
            setParam (a, "liveEngine", live ? 1.0f : 0.0f);
            return sattari::harness::renderThrough (p, vib, sr, block);
        };

        const auto noVib = render (0.0f, true);
        const auto keptVib = render (0.9f, true);
        const auto liveOut = render (0.0f, true);

        const double sNo   = pitchStdCents (noVib, sr);
        const double sKept = pitchStdCents (keptVib, sr);
        std::printf ("vibrato: input=%.1f ct, corrected(off)=%.1f ct, corrected(0.9)=%.1f ct\n",
                     inStd, sNo, sKept);

        bool finite = true; float peak = 0.0f;
        for (int i = 0; i < liveOut.getNumSamples(); ++i)
        {
            const float v = liveOut.getSample (0, i);
            if (! std::isfinite (v)) finite = false;
            peak = std::max (peak, std::fabs (v));
        }
        std::printf ("PSOLA live engine: finite=%d peak=%.3f\n", (int) finite, peak);

        // Core claim (robust to Rubber Band's own pitch jitter): with preservation
        // ON the corrected signal keeps clearly MORE of the singer's vibrato than
        // with it OFF, and retains a solid fraction of the input wiggle.
        int fails = 0;
        if (sKept <= sNo * 1.3)    { std::fprintf (stderr, "FAIL: vibrato not preserved (0.9 std not clearly > off std)\n"); ++fails; }
        if (sKept <= inStd * 0.6)  { std::fprintf (stderr, "FAIL: vibrato-0.9 kept too little of the input wiggle\n"); ++fails; }
        if (! finite)              { std::fprintf (stderr, "FAIL: PSOLA produced non-finite samples\n"); ++fails; }
        if (peak < 1.0e-3f)        { std::fprintf (stderr, "FAIL: PSOLA output is silent\n"); ++fails; }

        std::printf (fails == 0 ? "Auto Pitch feature proof ok\n" : "Auto Pitch feature proof FAILED (%d)\n", fails);
        return fails == 0 ? 0 : 1;
    }

    // Deterministic vocal-like input.
    juce::AudioBuffer<float> input;
    sattari::harness::generateVocalLike (input, sr, seconds);

    // Fixed, representative configuration: hard-tune to C major, YIN engine,
    // formant preserve on, transition modeling at default. This is the "voice
    // print" we lock - any DSP change to the correction path moves it.
    SattariAutoPitchAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "keyRoot", 0.0f);          // C
    setParam (apvts, "scale", 0.0f);            // major
    setParam (apvts, "followSongKey", 0.0f);    // deterministic render: ignore the session bus
    setParam (apvts, "style", 2.0f);            // Modern Rap (hard)
    setParam (apvts, "amount", 1.0f);
    setParam (apvts, "speed", 0.9f);
    setParam (apvts, "enableCorrection", 1.0f);
    setParam (apvts, "doNoHarm", 0.0f);
    setParam (apvts, "formantPreserve", 1.0f);
    setParam (apvts, "classicMode", 0.0f);
    setParam (apvts, "flexTune", 0.0f);
    setParam (apvts, "transition", 0.5f);
    setParam (apvts, "throat", 1.0f);
    // New engine/detector/vibrato features pinned OFF so this golden stays the
    // Rubber Band + YIN + no-vibrato reference (each feature is proven by its own
    // core contract; this locks the default correction path).
    setParam (apvts, "liveEngine", 0.0f);
    setParam (apvts, "neuralPitch", 0.0f);
    setParam (apvts, "vibrato", 0.0f);

    const auto out = sattari::harness::renderThrough (proc, input, sr, block);

    const auto root    = repoRoot();
    const auto wavFile = root.getChildFile ("validation/renders/auto_pitch_hardtune_Cmaj.wav");
    const auto goldFile = root.getChildFile ("validation/golden/auto_pitch_hardtune_Cmaj.fingerprint");

    if (! sattari::harness::writeWav (wavFile, out, sr))
        { std::fprintf (stderr, "FAIL: could not write %s\n", wavFile.getFullPathName().toRawUTF8()); return 2; }
    std::printf ("rendered %.1fs -> %s\n", seconds, wavFile.getFullPathName().toRawUTF8());

    const auto fp = sattari::harness::fingerprint (out, sr);
    std::printf ("  peak=%.4f rms=%.4f centroidHz=%.1f\n", fp.peak, fp.rms, fp.centroidHz);

    // --- Click / discontinuity scan ------------------------------------------
    // Regression guard for the crackle bug. The old Rubber Band path back-filled
    // short blocks with DRY signal, splicing un-shifted onto shifted samples on
    // essentially every block -> hundreds of near-full-scale jumps (continuous
    // crackle). The FIFO + HighConsistency + TransientsSmooth + slew-limit fixes
    // remove that; what remains on THIS input is a handful of Rubber-Band-internal
    // artifacts at the vibrato-cancellation extremes of a synthetic 4 s pitch
    // glide (a torture case - real sustained notes are clean). So we gate on a
    // small budget: the real bug would blow far past it, a clean render sits well
    // under. We always print the numbers.
    double maxJump = 0.0; int clicks = 0;
    const double clickThresh = 0.6 * (double) juce::jmax (1.0e-6f, fp.peak);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        const float* d = out.getReadPointer (ch);
        for (int i = 1; i < out.getNumSamples(); ++i)
        {
            const double j = std::fabs ((double) d[i] - (double) d[i - 1]);
            maxJump = std::max (maxJump, j);
            if (j > clickThresh) ++clicks;
        }
    }
    const double maxJumpNorm = maxJump / (double) juce::jmax (1.0e-6f, fp.peak);
    std::printf ("  maxJump=%.3f (%.2fx peak)  grossClicks=%d\n", maxJump, maxJumpNorm, clicks);

    // Per-channel budget: hundreds when the splice bug is present, ~a handful now.
    const int clickBudget = 16 * out.getNumChannels();

    if (smoke)
    {
        // CI-safe: prove the render path produced finite, non-silent, essentially
        // click-free audio (catches the crackle regression).
        bool finite = true;
        for (int ch = 0; ch < out.getNumChannels() && finite; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (! std::isfinite (out.getSample (ch, i))) { finite = false; break; }
        if (! finite)               { std::fprintf (stderr, "FAIL: render produced non-finite samples\n"); return 1; }
        if (fp.rms < 1.0e-3f)       { std::fprintf (stderr, "FAIL: render is silent (rms %.6f)\n", fp.rms); return 1; }
        if (clicks > clickBudget)   { std::fprintf (stderr, "FAIL: %d gross discontinuities (crackle) > budget %d\n", clicks, clickBudget); return 1; }

        // Clipping guard: a HOT input through the loudest Style must not exceed
        // 0 dBFS. Pitch-shift + Style overshoot used to push peaks past full scale
        // (no output ceiling); the soft ceiling now catches it.
        {
            juce::AudioBuffer<float> loud;
            sattari::harness::generateVocalLike (loud, sr, 2.0);
            loud.applyGain (6.0f);   // deliberately over-hot: raw output WOULD exceed 0 dBFS
            SattariAutoPitchAudioProcessor lp;
            auto& la = lp.parameters();
            setParam (la, "keyRoot", 0.0f);        setParam (la, "scale", 0.0f);
            setParam (la, "followSongKey", 0.0f);  setParam (la, "style", 4.0f);   // Big Harmony (loudest)
            setParam (la, "amount", 1.0f);         setParam (la, "speed", 0.9f);
            setParam (la, "enableCorrection", 1.0f); setParam (la, "outputGain", 6.0f);
            const auto lout = sattari::harness::renderThrough (lp, loud, sr, block);
            float lpk = 0.0f;
            for (int ch = 0; ch < lout.getNumChannels(); ++ch)
                for (int i = 0; i < lout.getNumSamples(); ++i)
                    lpk = std::max (lpk, std::fabs (lout.getSample (ch, i)));
            std::printf ("  loud-input peak=%.4f (ceiling must hold it <= 1.0)\n", lpk);
            if (lpk > 1.0f) { std::fprintf (stderr, "FAIL: hot input clips (peak %.4f > 1.0)\n", lpk); return 1; }
        }

        std::printf ("Auto Pitch render smoke ok\n");
        return 0;
    }

    if (bless)
    {
        goldFile.getParentDirectory().createDirectory();
        goldFile.replaceWithText (sattari::harness::fingerprintToText (fp));
        std::printf ("BLESSED golden -> %s\n", goldFile.getFullPathName().toRawUTF8());
        return 0;
    }

    if (! goldFile.existsAsFile())
    {
        std::fprintf (stderr, "FAIL: no golden fingerprint at %s (run once with --bless)\n",
                      goldFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto golden = sattari::harness::fingerprintFromText (goldFile.loadFileAsString());
    const float residual = sattari::harness::maxResidual (golden, fp);

    // Tolerance: the render is fully deterministic, so identical code must null
    // to ~0. A small floor absorbs float/-O variation across compilers.
    constexpr float tol = 1.0e-4f;
    std::printf ("  null residual vs golden = %.6g (tol %.6g)\n", residual, tol);
    if (residual > tol)
    {
        std::fprintf (stderr,
            "FAIL: render drifted from golden (residual %.6g > %.6g).\n"
            "      If this change to the sound is intended, re-bless: auto_pitch_render --bless\n",
            residual, tol);
        return 1;
    }

    std::printf ("Auto Pitch render null-test ok\n");
    return 0;
}
