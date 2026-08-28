#pragma once

// =============================================================================
//  Sattari Audio Suite — chromagram mapping (pure std, no JUCE/FFT)
// -----------------------------------------------------------------------------
//  Maps an FFT magnitude spectrum to a 12-bin chroma (energy per pitch class).
//  The caller owns the FFT (juce::dsp::FFT) and the time accumulation; this is
//  just the bin->pitch-class fold, kept pure so the part that's easy to get
//  wrong (octave wrap, off-by-one pitch class) is unit-testable. Feeds the
//  shared key detector (SattariKeyDetector.h) for the "scan the master → key →
//  publish to the whole suite" flow.
// =============================================================================

#include <algorithm>
#include <cmath>

namespace sattari::dsp
{

// Add a magnitude spectrum's energy into a 12-bin chroma (power per pitch class).
// `binHz` = sampleRate / fftSize. Bins outside [minHz, maxHz] (musical range)
// are ignored. `chroma` is accumulated into, so callers can roll it over time.
inline void accumulateChroma (const float* magnitude, int numBins, double binHz,
                              double chroma[12], double minHz = 55.0, double maxHz = 4186.0) noexcept
{
    if (binHz <= 0.0 || numBins <= 1) return;
    const int firstBin = std::max (1, (int) std::ceil (minHz / binHz));
    const int lastBin  = std::min (numBins - 1, (int) std::floor (maxHz / binHz));
    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const double f    = (double) bin * binHz;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const int    pc   = (((int) std::round (midi)) % 12 + 12) % 12;
        const double m    = (double) magnitude[bin];
        chroma[pc] += m * m;   // power
    }
}

// Roll a fresh chroma frame into a running histogram (exponential smoothing) and
// renormalise to sum 1, so the key estimate stays stable while a song plays.
inline void blendChroma (double running[12], const double frame[12], double blend) noexcept
{
    double total = 0.0;
    for (int i = 0; i < 12; ++i) total += frame[i];
    if (total < 1.0e-12) return;
    for (int i = 0; i < 12; ++i)
        running[i] = (1.0 - blend) * running[i] + blend * (frame[i] / total);
}

} // namespace sattari::dsp
