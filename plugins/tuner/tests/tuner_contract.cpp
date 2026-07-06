// Sattari Tuner — functional contract.
// Proves the tuner (1) reports the correct note + near-zero cents on a known
// reference tone, (2) tracks a detuned tone's cents offset, (3) respects the
// Reference-A control, and (4) passes audio through completely untouched.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Feed `hz` for `seconds` through the tuner; capture the final live reading and
// the max sample delta between input and output (must be 0 — pass-through).
SattariTunerAudioProcessor::LiveReading
run (double hz, double refA, int transpose, double& maxPassThruDelta)
{
    const double sr = 48000.0; const int block = 512;
    SattariTunerAudioProcessor p; auto& a = p.parameters();
    setParam (a, "referenceA", (float) refA);
    setParam (a, "smoothing", 0.3f);
    setParam (a, "transpose", (float) transpose);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * 1.5);
    maxPassThruDelta = 0.0;
    double ph = 0.0;
    juce::MidiBuffer midi;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = 0.4f * (float) std::sin (ph);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            ph += 2.0 * M_PI * hz / sr;
        }
        // Snapshot input, process, compare (pass-through must be exact).
        juce::AudioBuffer<float> in; in.makeCopyOf (buf);
        p.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
            maxPassThruDelta = std::max (maxPassThruDelta,
                (double) std::fabs (in.getSample (0, i) - buf.getSample (0, i)));
    }
    return p.getReading();
}
} // namespace

int main()
{
    int fails = 0;

    // 1) A4 = 440 Hz at reference 440 -> note 69 (A4), ~0 cents.
    double d1 = 0.0;
    auto a4 = run (440.0, 440.0, 0, d1);
    std::printf ("A440: voiced=%d note=%d (%s) cents=%.1f  passThruDelta=%.2e\n",
                 (int) a4.voiced, a4.midiNote,
                 SattariTunerAudioProcessor::noteName (a4.midiNote).toRawUTF8(), a4.cents, d1);
    if (! a4.voiced)                 { std::printf ("  FAIL A440 not voiced\n"); ++fails; }
    if (a4.midiNote != 69)           { std::printf ("  FAIL A440 note != 69\n"); ++fails; }
    if (std::fabs (a4.cents) > 8.0f) { std::printf ("  FAIL A440 cents off\n"); ++fails; }
    if (d1 > 1.0e-6)                 { std::printf ("  FAIL A440 not pass-through\n"); ++fails; }

    // 2) A detuned ~+30 cents from A4 (440 * 2^(30/1200) = 447.7 Hz) reads sharp.
    double d2 = 0.0;
    auto sharp = run (440.0 * std::pow (2.0, 30.0 / 1200.0), 440.0, 0, d2);
    std::printf ("A+30c: note=%d cents=%.1f\n", sharp.midiNote, sharp.cents);
    if (sharp.midiNote != 69)        { std::printf ("  FAIL +30c wrong note\n"); ++fails; }
    if (sharp.cents < 18.0f)         { std::printf ("  FAIL +30c not read as sharp\n"); ++fails; }

    // 3) Reference A = 432: a 432 Hz tone should now read A4 ~0 cents.
    double d3 = 0.0;
    auto a432 = run (432.0, 432.0, 0, d3);
    std::printf ("Ref432: note=%d cents=%.1f\n", a432.midiNote, a432.cents);
    if (a432.midiNote != 69)         { std::printf ("  FAIL ref432 note != 69\n"); ++fails; }
    if (std::fabs (a432.cents) > 8.0f){ std::printf ("  FAIL ref432 cents off\n"); ++fails; }

    // 4) Transpose +2 semitones: 440 Hz reads as B4 (71) instead of A4.
    double d4 = 0.0;
    auto tr = run (440.0, 440.0, 2, d4);
    std::printf ("Transpose+2: note=%d (%s)\n", tr.midiNote,
                 SattariTunerAudioProcessor::noteName (tr.midiNote).toRawUTF8());
    if (tr.midiNote != 71)           { std::printf ("  FAIL transpose+2 note != 71\n"); ++fails; }

    std::printf (fails == 0 ? "Tuner contract ok\n" : "Tuner contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
