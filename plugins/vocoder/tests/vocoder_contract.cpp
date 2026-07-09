// Sattari Vocoder — functional contract.
// Proves the classic vocoder behaviour end to end:
//   (1) with a steady internal carrier, the wet output amplitude tracks the
//       modulator (vocal) envelope — a silent modulator yields ~silent output,
//       and a loud modulator yields clearly more output.
//   (2) the output stays bounded (no runaway feedback / blow-up).
//   (3) dry/wet = dry (mix = 0) passes the modulator through unchanged.
//   (4) no NaNs / infs anywhere in the output.
//
// The carrier is internal (auto-voiced from the current key), so the test only
// feeds the modulator on the audio input, exactly as a DAW would.

#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

// Run `seconds` of audio through the vocoder. `modAt(t)` returns the modulator
// sample at time t (seconds). Fills `out` (stereo) with the produced signal.
void run (SattariVocoderAudioProcessor& p, double sr, double seconds,
          const std::function<float(double)>& modAt, juce::AudioBuffer<float>& out)
{
    const int block = 256;
    const int n = (int) (sr * seconds);
    out.setSize (2, n);
    out.clear();
    juce::MidiBuffer midi;
    int idx = 0;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const double t = (double) idx / sr;
            const float s = modAt (t);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
            ++idx;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
                out.setSample (ch, start + i, buf.getSample (ch, i));
    }
}

double rmsRange (const juce::AudioBuffer<float>& b, double sr, double t0, double t1)
{
    const int from = std::max (0, (int) (t0 * sr));
    const int to   = std::min (b.getNumSamples(), (int) (t1 * sr));
    if (to <= from) return 0.0;
    double e = 0.0;
    const float* d = b.getReadPointer (0);
    for (int i = from; i < to; ++i) e += (double) d[i] * d[i];
    return std::sqrt (e / (to - from));
}

double peakAbs (const juce::AudioBuffer<float>& b)
{
    double mx = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* d = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i) mx = std::max (mx, (double) std::fabs (d[i]));
    }
    return mx;
}

bool anyNaN (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* d = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (std::isnan (d[i]) || std::isinf (d[i])) return true;
    }
    return false;
}
} // namespace

int main()
{
    const double sr = 48000.0;
    int fails = 0;

    // A broadband, amplitude-enveloped modulator: white noise gated LOUD for the
    // first second, SILENT for the second. Broadband so it excites every band; a
    // saw carrier is broadband too, so the bands overlap and the vocoder speaks.
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> nd (-1.0f, 1.0f);
    // Pre-generate a deterministic noise table so both channels/segments agree.
    std::vector<float> noise ((size_t) (sr * 2.0) + 16);
    for (auto& x : noise) x = nd (rng);
    auto noiseAt = [&noise, sr] (double t) -> float
    {
        const int i = (int) (t * sr);
        return (i >= 0 && i < (int) noise.size()) ? noise[(size_t) i] : 0.0f;
    };
    // envelope: 0.6 amplitude for t<1.0, 0 for t>=1.0
    auto modLoudThenSilent = [&noiseAt] (double t) -> float
    { return t < 1.0 ? 0.6f * noiseAt (t) : 0.0f; };

    // ---- Test 1: wet output tracks the modulator envelope --------------------
    {
        SattariVocoderAudioProcessor p; auto& a = p.parameters();
        sp (a, "mix", 1.0f);          // fully wet
        sp (a, "unvoiced", 0.0f);     // isolate the voiced (band-vocoded) path
        sp (a, "bands", 20.0f);
        sp (a, "voicing", 1.0f);      // octaves — energetic carrier
        sp (a, "output", 0.0f);
        p.prepareToPlay (sr, 256);

        juce::AudioBuffer<float> out;
        run (p, sr, 2.0, modLoudThenSilent, out);

        // Measure after filter settling on the loud part; measure well into silence.
        const double loud   = rmsRange (out, sr, 0.3, 0.95);
        const double silent = rmsRange (out, sr, 1.3, 1.95);
        std::printf ("track: loudRMS=%.5f silentRMS=%.6f\n", loud, silent);

        if (! (loud > 1.0e-3)) { std::printf ("  FAIL vocoder produced no output on a loud modulator\n"); ++fails; }
        if (! (loud > silent * 8.0)) { std::printf ("  FAIL output did not track the modulator envelope (silence not quiet)\n"); ++fails; }
        if (! (silent < 5.0e-3)) { std::printf ("  FAIL silent modulator did not give ~silent output\n"); ++fails; }

        if (anyNaN (out)) { std::printf ("  FAIL NaN/inf in wet output\n"); ++fails; }
        const double pk = peakAbs (out);
        std::printf ("track: peak=%.4f\n", pk);
        if (! (pk < 4.5)) { std::printf ("  FAIL wet output not bounded\n"); ++fails; }
    }

    // ---- Test 2: output bounded with an extreme, sustained-loud modulator ----
    {
        SattariVocoderAudioProcessor p; auto& a = p.parameters();
        sp (a, "mix", 1.0f);
        sp (a, "unvoiced", 1.0f);     // consonant path fully open too
        sp (a, "bands", 32.0f);
        sp (a, "voicing", 4.0f);      // seventh — most carrier voices
        sp (a, "output", 12.0f);      // +12 dB output push
        p.prepareToPlay (sr, 256);

        juce::AudioBuffer<float> out;
        run (p, sr, 1.5, [&noiseAt] (double t) { return 0.95f * noiseAt (t); }, out);

        const double pk = peakAbs (out);
        std::printf ("bounded: peak=%.4f\n", pk);
        if (anyNaN (out)) { std::printf ("  FAIL NaN/inf under extreme drive\n"); ++fails; }
        if (! (pk <= 4.0 + 1.0e-4)) { std::printf ("  FAIL output exceeded the safety clamp\n"); ++fails; }
        if (! (pk > 0.01)) { std::printf ("  FAIL expected audible output under drive\n"); ++fails; }
    }

    // ---- Test 3: dry/wet = dry passes the modulator ---------------------------
    {
        SattariVocoderAudioProcessor p; auto& a = p.parameters();
        sp (a, "mix", 0.0f);          // fully dry
        sp (a, "unvoiced", 0.0f);
        sp (a, "output", 0.0f);
        p.prepareToPlay (sr, 256);

        // Steady tone modulator, easy to compare in/out.
        const double f = 220.0;
        auto tone = [f, sr] (double t) { return 0.5f * (float) std::sin (2.0 * M_PI * f * t); };

        juce::AudioBuffer<float> out;
        run (p, sr, 0.5, tone, out);

        // Compare out vs the modulator we fed, sample-aligned, after settling.
        const int from = (int) (0.2 * sr), to = (int) (0.45 * sr);
        double err = 0.0, sig = 0.0;
        const float* d = out.getReadPointer (0);
        for (int i = from; i < to; ++i)
        {
            const double t = (double) i / sr;
            const double m = 0.5 * std::sin (2.0 * M_PI * f * t);
            err += (d[i] - m) * (d[i] - m);
            sig += m * m;
        }
        const double nrmse = std::sqrt (err / std::max (1.0, sig));
        std::printf ("dry: nrmse(out vs modulator)=%.5f\n", nrmse);
        if (anyNaN (out)) { std::printf ("  FAIL NaN/inf in dry output\n"); ++fails; }
        if (! (nrmse < 1.0e-3)) { std::printf ("  FAIL mix=0 did not pass the modulator through\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Vocoder contract ok\n" : "Vocoder contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
