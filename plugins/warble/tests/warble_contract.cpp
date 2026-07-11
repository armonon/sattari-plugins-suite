// Sattari Warble — functional contract.
// Proves:
//   1) mix=0 is an exact dry pass-through (bit-for-bit within a tiny epsilon).
//   2) Age=0 is a pass-through even at mix=1 (all degradation stages scale by Age).
//   3) Age>0 measurably colours the signal (wow/flutter pitch wobble makes the
//      output differ from the dry input) AND the tape saturation compresses
//      peaks (a hot input's peak comes out lower), while the output stays bounded
//      (|out| <= 1).
//   4) The output never contains NaN/Inf.
//   5) With a SILENT input, the hiss + crackle noise floor stays low-level (it
//      does not swamp the signal — the RMS is well under a loud reference).
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <functional>

namespace
{
void sp (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{ if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); }

struct Stats
{
    double maxDryErr = 0.0;   // max |out - dryInput| after a settle window
    double outPeak   = 0.0;   // peak |out|
    double inPeak    = 0.0;   // peak |dry input|
    double outRms    = 0.0;   // rms of out
    bool   sawNaN    = false;
};

// Render `seconds` of a 300 Hz tone (amp `amp`) through a freshly-prepared
// processor configured by `setup`. When amp==0 the input is silence.
Stats render (std::function<void(juce::AudioProcessorValueTreeState&)> setup,
              double seconds, float amp)
{
    const double sr = 48000.0; const int block = 512;
    SattariWarbleAudioProcessor p; auto& a = p.parameters();
    setup (a);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * seconds);
    const int settle = (int) (sr * 0.15);   // skip filter/delay transient
    Stats st;
    double ph = 0.0; juce::MidiBuffer midi;
    double sumSq = 0.0; int cnt = 0;

    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len), in (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = amp * (float) std::sin (ph);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            in.setSample  (0, i, s); in.setSample  (1, i, s);
            ph += 2.0 * M_PI * 300.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                const int gi = start + i;
                const float o = buf.getSample (ch, i);
                const float d = in.getSample  (ch, i);
                if (std::isnan (o) || std::isinf (o)) st.sawNaN = true;
                st.outPeak = std::max (st.outPeak, (double) std::fabs (o));
                st.inPeak  = std::max (st.inPeak,  (double) std::fabs (d));
                if (gi >= settle)
                {
                    st.maxDryErr = std::max (st.maxDryErr, (double) std::fabs (o - d));
                    sumSq += (double) o * o; ++cnt;
                }
            }
    }
    st.outRms = cnt ? std::sqrt (sumSq / cnt) : 0.0;
    return st;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    int fails = 0;

    // 1) mix=0 -> dry pass-through (all effects at full trim + Age, mix silences).
    {
        auto s = render ([] (auto& a){
            sp (a, "age", 1.0f); sp (a, "wow", 1.0f); sp (a, "flutter", 1.0f);
            sp (a, "sat", 1.0f); sp (a, "hiss", 1.0f); sp (a, "crackle", 1.0f);
            sp (a, "mix", 0.0f); sp (a, "output", 0.0f);
        }, 0.5, 0.5f);
        std::printf ("mix=0 dryErr=%.2e  nan=%d\n", s.maxDryErr, (int) s.sawNaN);
        if (s.maxDryErr > 5.0e-3) { std::printf ("  FAIL mix=0 not pass-through\n"); ++fails; }
        if (s.sawNaN)             { std::printf ("  FAIL NaN at mix=0\n"); ++fails; }
    }

    // 2) Age=0 -> pass-through even at full mix (every stage scales by Age).
    {
        auto s = render ([] (auto& a){
            sp (a, "age", 0.0f); sp (a, "wow", 1.0f); sp (a, "flutter", 1.0f);
            sp (a, "sat", 1.0f); sp (a, "hiss", 1.0f); sp (a, "crackle", 1.0f);
            sp (a, "mix", 1.0f); sp (a, "output", 0.0f);
        }, 0.5, 0.5f);
        std::printf ("age=0 dryErr=%.2e  nan=%d\n", s.maxDryErr, (int) s.sawNaN);
        if (s.maxDryErr > 5.0e-3) { std::printf ("  FAIL age=0 not pass-through\n"); ++fails; }
        if (s.sawNaN)             { std::printf ("  FAIL NaN at age=0\n"); ++fails; }
    }

    // 3) Age>0 colours the signal + saturates peaks, stays bounded, no NaN.
    //    Hot input (amp 0.9) with strong drive; wow/flutter guarantee the wet
    //    diverges from the dry tone. Hiss/crackle off so the peak test isolates
    //    saturation.
    {
        auto s = render ([] (auto& a){
            sp (a, "age", 1.0f); sp (a, "wow", 1.0f); sp (a, "flutter", 1.0f);
            sp (a, "sat", 1.0f); sp (a, "tone", 0.0f); sp (a, "hiss", 0.0f);
            sp (a, "crackle", 0.0f); sp (a, "mix", 1.0f); sp (a, "output", 0.0f);
        }, 0.6, 0.9f);
        std::printf ("age>0 colour dryErr=%.3f  inPeak=%.3f outPeak=%.3f  nan=%d\n",
                     s.maxDryErr, s.inPeak, s.outPeak, (int) s.sawNaN);
        if (s.maxDryErr < 1.0e-2) { std::printf ("  FAIL Age>0 did not colour the signal\n"); ++fails; }
        if (s.outPeak >= s.inPeak){ std::printf ("  FAIL saturation did not compress peaks\n"); ++fails; }
        if (s.outPeak > 1.0001)   { std::printf ("  FAIL output not bounded\n"); ++fails; }
        if (s.sawNaN)             { std::printf ("  FAIL NaN at Age>0\n"); ++fails; }
    }

    // 4) Silent input: hiss + crackle at full amount stay low-level (RMS well
    //    under a -20 dBFS reference of 0.1). Also bounded and NaN-free.
    {
        auto s = render ([] (auto& a){
            sp (a, "age", 1.0f); sp (a, "wow", 0.0f); sp (a, "flutter", 0.0f);
            sp (a, "sat", 0.0f); sp (a, "tone", 0.0f); sp (a, "hiss", 1.0f);
            sp (a, "crackle", 1.0f); sp (a, "mix", 1.0f); sp (a, "output", 0.0f);
        }, 1.0, 0.0f);
        std::printf ("noise floor rms=%.4f  peak=%.4f  nan=%d\n", s.outRms, s.outPeak, (int) s.sawNaN);
        if (s.outRms > 0.05)  { std::printf ("  FAIL noise floor too loud\n"); ++fails; }
        if (s.outPeak > 0.6)  { std::printf ("  FAIL noise peak too loud\n"); ++fails; }
        if (s.sawNaN)         { std::printf ("  FAIL NaN in noise floor\n"); ++fails; }
    }

    std::printf (fails == 0 ? "Warble contract ok\n" : "Warble contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
