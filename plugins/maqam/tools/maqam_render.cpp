// Sattari Maqam - offline render + microtonal-correction proof.
//
// Synthesises a steady tone 40 cents sharp of Rast's neutral 3rd on C (a
// quarter-tone target at MIDI 63.5), renders it through the real audio path,
// then MEASURES the output pitch and asserts it snapped onto the half-flat
// degree - i.e. the quarter-tone correction actually works on audio, not just
// in the unit-tested math. A 12-TET tuner would land on 63 or 64; only a real
// maqam tuner lands near 63.5.
//
//   maqam_render          # render + measure + assert the snap (local proof)
//   maqam_render --smoke  # render + assert finite/non-silent only (CI-safe)
#include "MaqamProcessor.h"
#include <sattari_audio_core/SattariRenderHarness.h>
#include <sattari_audio_core/SattariPitchDetector.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        std::fprintf (stderr, "WARNING: missing parameter %s\n", id.toRawUTF8());
}
double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
double hzToMidi (double h) { return 69.0 + 12.0 * std::log2 (h / 440.0); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);

    const double sr = 48000.0;
    const int    block = 512;
    const double seconds = 3.0;

    const double targetMidi = 63.5;   // Rast neutral 3rd on C (half-flat E)
    const double inMidi      = 63.9;   // 40 cents sharp of it
    const double inHz        = midiToHz (inMidi);

    // Steady stereo tone (a little harmonic content so the tract is realistic).
    juce::AudioBuffer<float> input;
    const int n = (int) std::round (seconds * sr);
    input.setSize (2, n, false, false, true);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float s = (float) (0.5 * std::sin (ph) + 0.12 * std::sin (2.0 * ph) + 0.06 * std::sin (3.0 * ph));
        input.setSample (0, i, s);
        input.setSample (1, i, s);
        ph += 2.0 * juce::MathConstants<double>::pi * inHz / sr;
    }

    SattariMaqamAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "maqam", 0.0f);       // Rast
    setParam (apvts, "root", 0.0f);        // C
    setParam (apvts, "followKey", 0.0f);   // manual root
    setParam (apvts, "strength", 1.0f);    // full correction
    setParam (apvts, "mix", 1.0f);         // fully wet
    setParam (apvts, "formant", 1.0f);
    setParam (apvts, "lowLatency", 0.0f);

    const auto out = sattari::harness::renderThrough (proc, input, sr, block);
    proc.releaseResources();

    // Finite + non-silent.
    bool finite = true; float peak = 0.0f;
    for (int ch = 0; ch < out.getNumChannels() && finite; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
        {
            const float v = out.getSample (ch, i);
            if (! std::isfinite (v)) { finite = false; break; }
            peak = std::max (peak, std::abs (v));
        }
    if (! finite)     { std::fprintf (stderr, "FAIL: non-finite output\n"); return 1; }
    if (peak < 1.0e-3f) { std::fprintf (stderr, "FAIL: silent output (peak %.6f)\n", peak); return 1; }

    if (smoke)
    {
        std::printf ("Maqam render smoke ok (peak %.3f)\n", peak);
        return 0;
    }

    // Measure the corrected output pitch over the steady tail.
    sattari::dsp::PitchDetector det;
    det.prepare (sr, 50.0f, 1000.0f);
    det.pushBlock (out.getReadPointer (0), out.getNumSamples());
    const auto res = det.analyse (0.2f);
    if (! res.voiced) { std::fprintf (stderr, "FAIL: could not measure output pitch\n"); return 1; }

    const double outMidi   = hzToMidi (res.frequencyHz);
    const double errToDeg  = std::abs (outMidi - targetMidi) * 100.0;   // cents from the maqam degree
    const double inErr     = std::abs (inMidi - targetMidi) * 100.0;    // 40 cents
    const double distToSemi = std::abs (outMidi - std::round (outMidi)) * 100.0;

    std::printf ("Maqam render: in %.1f cents off degree -> out %.2f cents off degree (out MIDI %.3f, %.0f cents from nearest semitone)\n",
                 inErr, errToDeg, outMidi, distToSemi);

    if (errToDeg > 25.0)
    {
        std::fprintf (stderr, "FAIL: output did not snap to the maqam degree (%.1f cents off > 25)\n", errToDeg);
        return 1;
    }
    if (errToDeg >= inErr)
    {
        std::fprintf (stderr, "FAIL: correction did not reduce the error (%.1f >= %.1f cents)\n", errToDeg, inErr);
        return 1;
    }
    if (distToSemi < 30.0)
    {
        std::fprintf (stderr, "FAIL: landed near a 12-TET semitone (%.0f cents) - quarter-tone snap not proven\n", distToSemi);
        return 1;
    }

    std::printf ("Maqam quarter-tone correction proof ok\n");
    return 0;
}
