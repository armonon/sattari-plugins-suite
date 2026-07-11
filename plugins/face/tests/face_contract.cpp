// Sattari Face — functional contract.
// Proves (1) Pitch shifts the fundamental (0 st keeps ~200 Hz, +12 st -> ~400 Hz),
// (2) a Formant shift changes the timbre while KEEPING the pitch, and (3) output
// stays finite + non-silent. Requires the Rubber Band engine.
#include "PluginProcessor.h"
#include <sattari_audio_core/SattariPitchDetector.h>
#include <cmath>
#include <cstdio>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render a harmonic-rich 200 Hz tone; return ch0 + measured fundamental + rms.
juce::AudioBuffer<float> render (float pitchSt, float formantSt, double sr,
                                 double& fundHz, double& rms, bool& finite)
{
    const int block = 512;
    SattariFaceAudioProcessor p; auto& a = p.parameters();
    setParam (a, "pitch", pitchSt);
    setParam (a, "formant", formantSt);
    setParam (a, "mix", 1.0f);
    setParam (a, "output", 0.0f);
    p.prepareToPlay (sr, block);

    const int n = (int) (sr * 2.0);
    juce::AudioBuffer<float> out (2, n);
    double ph = 0.0; juce::MidiBuffer midi; finite = true;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float s = 0.35f * (float) std::sin (ph) + 0.18f * (float) std::sin (2*ph)
                          + 0.10f * (float) std::sin (3*ph) + 0.06f * (float) std::sin (4*ph);
            buf.setSample (0, i, s); buf.setSample (1, i, s);
            ph += 2.0 * M_PI * 200.0 / sr;
        }
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < len; ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v)) finite = false;
                out.setSample (ch, start + i, v);
            }
    }

    // Detect fundamental + rms over the settled second half.
    sattari::dsp::PitchDetector det; det.prepare (sr, 60.0f, 1000.0f, 0.05f);
    const int from = n / 2; double s2 = 0.0; fundHz = 0.0; int nvoiced = 0;
    for (int start = from; start + block <= n; start += block)
    {
        det.pushBlock (out.getReadPointer (0) + start, block);
        const auto r = det.analyse (0.2f);
        if (r.voiced) { fundHz += r.frequencyHz; ++nvoiced; }
        for (int i = 0; i < block; ++i) s2 += (double) out.getSample (0, start + i) * out.getSample (0, start + i);
    }
    if (nvoiced) fundHz /= nvoiced;
    rms = std::sqrt (s2 / (n - from));
    return out;
}

double centroid (const juce::AudioBuffer<float>& b, double sr)
{
    // Crude spectral centroid via a bank of single-bin magnitudes.
    const int n = b.getNumSamples(); const int from = n / 2;
    const float* d = b.getReadPointer (0);
    double num = 0.0, den = 0.0;
    for (double f = 100.0; f <= 8000.0; f += 100.0)
    {
        double sSin = 0.0, sCos = 0.0;
        for (int i = from; i < n; ++i)
        { const double t = (double) i / sr; sSin += d[i] * std::sin (2*M_PI*f*t); sCos += d[i] * std::cos (2*M_PI*f*t); }
        const double mag = std::sqrt (sSin*sSin + sCos*sCos);
        num += f * mag; den += mag;
    }
    return den > 0.0 ? num / den : 0.0;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
   #if ! SATTARI_FACE_HAS_RUBBERBAND
    std::printf ("Face contract SKIPPED (built without Rubber Band)\n");
    return 0;
   #else
    const double sr = 48000.0;
    int fails = 0;
    double f0 = 0, r0 = 0, f12 = 0, r12 = 0, ffm = 0, rfm = 0;
    bool q0 = true, q12 = true, qfm = true;

    auto base   = render (0.0f,  0.0f, sr, f0,  r0,  q0);
    auto up     = render (12.0f, 0.0f, sr, f12, r12, q12);
    auto formUp = render (0.0f,  7.0f, sr, ffm, rfm, qfm);

    const double cBase = centroid (base, sr), cForm = centroid (formUp, sr);
    std::printf ("pitch0: fund=%.1f rms=%.3f | pitch+12: fund=%.1f | formant+7: fund=%.1f centroid %.0f->%.0f\n",
                 f0, r0, f12, ffm, cBase, cForm);

    if (! (q0 && q12 && qfm))         { std::printf ("  FAIL non-finite\n"); ++fails; }
    if (r0 < 1.0e-3)                  { std::printf ("  FAIL output silent\n"); ++fails; }
    if (std::fabs (f0 - 200.0) > 30.0){ std::printf ("  FAIL pitch0 fundamental drifted\n"); ++fails; }
    if (std::fabs (f12 - 400.0) > 60.0){ std::printf ("  FAIL pitch+12 did not double the fundamental\n"); ++fails; }
    if (std::fabs (ffm - 200.0) > 35.0){ std::printf ("  FAIL formant shift changed the pitch\n"); ++fails; }
    if (cForm <= cBase * 1.05)        { std::printf ("  FAIL formant+7 did not raise the timbre/centroid\n"); ++fails; }

    std::printf (fails == 0 ? "Face contract ok\n" : "Face contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
   #endif
}
