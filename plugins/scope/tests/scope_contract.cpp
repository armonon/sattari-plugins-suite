// Sattari Scope — functional contract.
// Proves the analyser is (1) a bit-exact pass-through (audio is NEVER altered),
// (2) produces a finite, non-degenerate spectrum for a tone with a peak near the
// tone's log band, (3) never emits NaN/Inf, and (4) stays finite on silence
// (no divide-by-zero).
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 512;
constexpr int    kFft   = SattariScopeAudioProcessor::kFftSize;
constexpr int    kBands = SattariScopeAudioProcessor::kNumBands;

// Feed `seconds` of a stereo tone (or silence if amp==0) through the processor.
// Accumulates the worst pass-through error, then forces ONE synchronous analysis
// pass (analyseNow) so the result is deterministic and worker-thread-independent.
SattariScopeAudioProcessor::Spectrum
run (float amp, double toneHz, double seconds, double& passErr, bool& anyNaN)
{
    SattariScopeAudioProcessor p;
    p.prepareToPlay (kSr, kBlock);

    const int n = (int) (kSr * seconds);
    double ph = 0.0; juce::MidiBuffer midi; passErr = 0.0; anyNaN = false;

    for (int start = 0; start < n; start += kBlock)
    {
        const int len = std::min (kBlock, n - start);
        juce::AudioBuffer<float> buf (2, len);
        juce::AudioBuffer<float> in  (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = amp * (float) std::sin (ph);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
            in.setSample (0, i, s);
            in.setSample (1, i, s);
            ph += 2.0 * M_PI * toneHz / kSr;
        }
        p.processBlock (buf, midi);

        // Pass-through must be BIT-EXACT: output == input, sample for sample.
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
                passErr = std::max (passErr,
                                    (double) std::fabs (buf.getSample (ch, i) - in.getSample (ch, i)));
    }

    // Stop the background worker so THIS thread is the sole writer of the
    // spectrum snapshot (single-writer seqlock contract) — deterministic result.
    p.releaseResources();
    p.analyseNow();
    auto sp = p.getSpectrum();
    for (int b = 0; b < kBands; ++b)
        if (! std::isfinite (sp.bandDb[b])) anyNaN = true;
    for (int i = 0; i < 12; ++i)
        if (! std::isfinite (sp.chroma[i])) anyNaN = true;
    return sp;
}

// Reproduce the processor's log band layout to know where a tone should land.
int expectedBandFor (double toneHz)
{
    const double binHz  = kSr / (double) kFft;
    const double loHz   = std::max (20.0, binHz);
    const double hiHz   = std::max (loHz * 2.0, kSr * 0.5);
    const double logLo  = std::log (loHz), logHi = std::log (hiHz);
    const double t      = (std::log (toneHz) - logLo) / (logHi - logLo);
    return (int) std::round (t * (double) kBands);
}

int peakBand (const SattariScopeAudioProcessor::Spectrum& s)
{
    int best = 0; float bv = -1.0e9f;
    for (int b = 0; b < kBands; ++b)
        if (s.bandDb[b] > bv) { bv = s.bandDb[b]; best = b; }
    return best;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    int fails = 0;

    // (1)+(2)+(3): a 1 kHz tone.
    double peTone = 0.0; bool nanTone = false;
    const double toneHz = 1000.0;
    auto tone = run (0.5f, toneHz, 2.0, peTone, nanTone);
    const int expBand  = expectedBandFor (toneHz);
    const int gotBand  = peakBand (tone);
    const float peakDb  = tone.bandDb[gotBand];

    // (4): silence.
    double peSil = 0.0; bool nanSil = false;
    auto sil = run (0.0f, toneHz, 1.0, peSil, nanSil);
    int silPeak = peakBand (sil);

    std::printf ("tone: peakBand=%d (exp~%d) peakDb=%.1f hasData=%d key=%d | "
                 "silence: peakDb=%.1f hasData=%d | passErr(tone)=%.1e passErr(sil)=%.1e\n",
                 gotBand, expBand, peakDb, (int) tone.hasData, tone.keyRoot,
                 sil.bandDb[silPeak], (int) sil.hasData, peTone, peSil);

    // (1) Bit-exact pass-through on tone AND silence.
    if (peTone > 0.0 || peSil > 0.0)
        { std::printf ("  FAIL not bit-exact pass-through\n"); ++fails; }

    // (3) No NaNs/Infs anywhere.
    if (nanTone || nanSil)
        { std::printf ("  FAIL non-finite spectrum data\n"); ++fails; }

    // (2) Spectrum has data and a clear peak near the tone's band.
    if (! tone.hasData)
        { std::printf ("  FAIL no spectrum data produced for a tone\n"); ++fails; }
    if (std::abs (gotBand - expBand) > 3)
        { std::printf ("  FAIL spectrum peak not near tone band (got %d, exp %d)\n", gotBand, expBand); ++fails; }

    // (2b) Non-degenerate: the peak must stand well above the noise floor.
    if (! (peakDb > -60.0f))
        { std::printf ("  FAIL tone peak too low / degenerate (%.1f dB)\n", peakDb); ++fails; }

    // (4) Silence stays finite and near the floor (no divide-by-zero blow-up).
    if (! std::isfinite (sil.bandDb[silPeak]) || sil.bandDb[silPeak] > -80.0f)
        { std::printf ("  FAIL silence did not stay at the floor (%.1f dB)\n", sil.bandDb[silPeak]); ++fails; }

    std::printf (fails == 0 ? "Scope contract ok\n" : "Scope contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
