// LoudnessMeter check (ITU-R BS.1770 / EBU R128). Verifies the hard numeric
// properties: +6 dB level -> +6 LU; stereo is +3 LU over mono; silence gates to
// the floor; gating ignores silent tails; true-peak >= sample peak and catches
// inter-sample overshoot a sample-peak reading misses.
#include "LoudnessMeter.h"
#include <iostream>
#include <string>
#include <cmath>

using namespace stemdeck;

namespace
{
int fails = 0;
void check (bool ok, const std::string& what)
{
    std::cout << "  " << (ok ? "OK  " : "FAIL") << "  " << what << "\n";
    if (! ok) ++fails;
}

// stereo/mono 1 kHz sine of `amp`, `secs` long; if mono, only channel 0 is driven.
juce::AudioBuffer<float> sine (double sr, double secs, double hz, double amp, bool stereo = true)
{
    const int n = (int) (sr * secs);
    juce::AudioBuffer<float> b (2, n); b.clear();
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
        b.setSample (0, i, v);
        if (stereo) b.setSample (1, i, v);
    }
    return b;
}
} // namespace

int main()
{
    const double SR = 48000.0;

    // ── +6 dB amplitude -> +6 LU ────────────────────────────────────────────────
    {
        const auto a = LoudnessMeter::measure (sine (SR, 3.0, 1000.0, 0.1), SR);
        const auto b = LoudnessMeter::measure (sine (SR, 3.0, 1000.0, 0.2), SR);  // +6.02 dB
        check (a.integratedLufs > -70.0 && b.integratedLufs > -70.0, "tones read above the gate");
        check (std::abs ((b.integratedLufs - a.integratedLufs) - 6.02) < 0.15, "doubling amplitude adds ~6 LU");
    }

    // ── Stereo is +3 LU over the same signal in one channel ─────────────────────
    {
        const auto mono   = LoudnessMeter::measure (sine (SR, 3.0, 1000.0, 0.2, false), SR);
        const auto stereo = LoudnessMeter::measure (sine (SR, 3.0, 1000.0, 0.2, true),  SR);
        check (std::abs ((stereo.integratedLufs - mono.integratedLufs) - 3.01) < 0.15, "stereo is ~+3 LU over mono");
    }

    // ── Monotonic + silence gates to the floor ──────────────────────────────────
    {
        const auto quiet = LoudnessMeter::measure (sine (SR, 3.0, 1000.0, 0.05), SR);
        const auto loud  = LoudnessMeter::measure (sine (SR, 3.0, 1000.0, 0.5),  SR);
        check (loud.integratedLufs > quiet.integratedLufs, "louder tone reads higher LUFS");
        juce::AudioBuffer<float> silent (2, (int) (SR * 3.0)); silent.clear();
        check (LoudnessMeter::measure (silent, SR).integratedLufs < -100.0, "silence gates to the floor");
    }

    // ── Gating ignores a silent tail (integrated ~ the loud section) ────────────
    {
        const auto loudOnly = LoudnessMeter::measure (sine (SR, 3.0, 1000.0, 0.3), SR);
        // loud 3 s then silent 3 s in one buffer.
        juce::AudioBuffer<float> mixed (2, (int) (SR * 6.0)); mixed.clear();
        const auto loud = sine (SR, 3.0, 1000.0, 0.3);
        for (int c = 0; c < 2; ++c) mixed.copyFrom (c, 0, loud, c, 0, loud.getNumSamples());
        const auto m = LoudnessMeter::measure (mixed, SR);
        check (std::abs (m.integratedLufs - loudOnly.integratedLufs) < 0.6, "silent tail is gated out (integrated ~ loud part)");
    }

    // ── True peak >= sample peak, and catches inter-sample overshoot ────────────
    {
        // 9.6 kHz (0.2*fs): sample spacing is 72 deg, so crests fall between samples
        // (sample peak < true peak) AND the 16-tap sinc reconstructs this band
        // accurately, so the meter recovers the real inter-sample peak. NOTE: the
        // sinc rolls off near Nyquist (see StemDeckResamplerTest), so true-peak can
        // UNDER-read on very bright > ~15 kHz content — a fully BS.1770-compliant
        // meter needs a dedicated flat-to-Nyquist 4x FIR. Known limitation.
        const auto hi = sine (SR, 1.0, 0.2 * SR, 0.7);   // 9.6 kHz @ 48k
        float samplePeak = 0.0f;
        for (int i = 0; i < hi.getNumSamples(); ++i) samplePeak = juce::jmax (samplePeak, std::abs (hi.getSample (0, i)));
        const auto tp = LoudnessMeter::measure (hi, SR).truePeakDb;
        const double samplePeakDb = 20.0 * std::log10 (juce::jmax (1e-9f, samplePeak));
        check (tp >= samplePeakDb - 0.01, "true-peak is never below the sample peak");
        check (tp > samplePeakDb + 0.05, "true-peak catches inter-sample overshoot the sample peak misses");
    }

    std::cout << (fails == 0 ? "LOUDNESS_OK" : "LOUDNESS_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
