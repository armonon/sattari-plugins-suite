#include <sattari_audio_core/SattariLivePitchEngine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
double tonePower (const std::vector<float>& x, int start, double sr, double hz)
{
    double re = 0.0, im = 0.0;
    const double w = 6.2831853071795864769 * hz / sr;
    for (int i = start; i < (int) x.size(); ++i)
    {
        re += (double) x[(size_t) i] * std::cos (w * (double) i);
        im -= (double) x[(size_t) i] * std::sin (w * (double) i);
    }
    return re * re + im * im;
}
}

int main()
{
    constexpr double sr = 48000.0;
    constexpr int total = 96000;
    constexpr int block = 64;
    constexpr double sourceHz = 220.0;
    constexpr double targetHz = 261.625565;
    sattari::dsp::ExtremePitchShifter shifter;
    shifter.prepare (sr, 512);
    std::vector<float> input ((size_t) total), output ((size_t) total);
    for (int i = 0; i < total; ++i)
        input[(size_t) i] = 0.2f * (float) std::sin (
            6.2831853071795864769 * sourceHz * (double) i / sr);
    const double ratio = targetHz / sourceHz;
    for (int offset = 0; offset < total; offset += block)
        shifter.process (input.data() + offset, output.data() + offset,
                         std::min (block, total - offset), ratio, true,
                         sr / sourceHz);

    double bestHz = 180.0, bestPower = -1.0;
    for (double hz = 180.0; hz <= 340.0; hz += 0.25)
    {
        const double p = tonePower (output, total / 2, sr, hz);
        if (p > bestPower) { bestPower = p; bestHz = hz; }
    }
    const double targetPower = tonePower (output, total / 2, sr, targetHz);
    const double sourcePower = tonePower (output, total / 2, sr, sourceHz);
    double outputEnergy = 0.0, differenceEnergy = 0.0;
    for (int i = total / 2; i < total; ++i)
    {
        outputEnergy += (double) output[(size_t) i] * output[(size_t) i];
        const double dry = (double) input[(size_t) (i - shifter.getLatencySamples())];
        const double d = (double) output[(size_t) i] - dry;
        differenceEnergy += d * d;
    }
    const double cents = 1200.0 * std::log2 (bestHz / targetHz);
    std::printf ("Extreme latency=%d samples best=%.2f Hz error=%.2f ct target/source=%.3f diffRms=%.5f outRms=%.5f\n",
                 shifter.getLatencySamples(), bestHz, cents,
                 targetPower / std::max (1.0e-20, sourcePower),
                 std::sqrt (differenceEnergy / (double) (total / 2)),
                 std::sqrt (outputEnergy / (double) (total / 2)));

    sattari::dsp::ExtremePitchShifter largeBlockShifter;
    largeBlockShifter.prepare (sr, 512);
    std::vector<float> largeBlockOutput ((size_t) total);
    for (int offset = 0; offset < total; offset += 512)
        largeBlockShifter.process (input.data() + offset, largeBlockOutput.data() + offset,
                                   std::min (512, total - offset), ratio, true,
                                   sr / sourceHz);
    double largeBestHz = 240.0, largeBestPower = -1.0;
    for (double hz = 240.0; hz <= 280.0; hz += 0.25)
    {
        const double p = tonePower (largeBlockOutput, total / 2, sr, hz);
        if (p > largeBestPower) { largeBestPower = p; largeBestHz = hz; }
    }
    double energy64 = 0.0, energy512 = 0.0, boundaryJump = 0.0;
    for (int i = total / 2; i < total; ++i)
    {
        energy64 += (double) output[(size_t) i] * output[(size_t) i];
        energy512 += (double) largeBlockOutput[(size_t) i] * largeBlockOutput[(size_t) i];
        if (i % block == 0 || i % 512 == 0)
            boundaryJump = std::max (boundaryJump,
                std::abs ((double) output[(size_t) i] - (double) output[(size_t) (i - 1)]));
    }
    const double blockPitchDelta = std::abs (1200.0 * std::log2 (bestHz / largeBestHz));
    const double blockEnergyRatio = std::sqrt (energy64 / std::max (1.0e-20, energy512));

    sattari::dsp::ExtremeLivePitchEngine acquisitionEngine;
    acquisitionEngine.prepare (sr, block, 70.0, 1100.0);
    sattari::dsp::LiveCorrectionPlanner::Settings settings;
    settings.forcedTargetMidi = 69;
    settings.amount = 1.0;
    settings.retuneMilliseconds = 1.5;
    settings.preserveVibrato = 0.0;
    settings.transitionAmount = 1.0;
    std::vector<float> acquisitionOut ((size_t) block);
    int voicedAt = -1, correctedAt = -1;
    double acquisitionPhase = 0.0;
    for (int offset = 0; offset < total / 2 && correctedAt < 0; offset += block)
    {
        std::vector<float> acquisitionIn ((size_t) block);
        for (int i = 0; i < block; ++i)
        {
            acquisitionIn[(size_t) i] = 0.2f * (float) std::sin (acquisitionPhase);
            acquisitionPhase += 6.2831853071795864769 * 456.0 / sr;
        }
        acquisitionEngine.processCorrected (acquisitionIn.data(), acquisitionOut.data(),
                                            block, settings);
        const auto pitch = acquisitionEngine.getPitchResult();
        if (pitch.voiced && voicedAt < 0) voicedAt = offset + block;
        if (pitch.voiced && std::abs (acquisitionEngine.getCorrectionCents()) >= 50.0)
            correctedAt = offset + block;
    }
    const double voicedMs = 1000.0 * (double) voicedAt / sr;
    const double correctedMs = 1000.0 * (double) correctedAt / sr;

    sattari::dsp::ExtremeLivePitchEngine lowRangeEngine;
    lowRangeEngine.prepare (sr, block, 70.0, 1100.0);
    std::vector<float> lowRangeIn ((size_t) block), lowRangeOut ((size_t) block);
    constexpr double lowRangeHz = 82.406889; // E2
    double lowRangePhase = 0.0;
    for (int offset = 0; offset < total; offset += block)
    {
        for (int i = 0; i < block; ++i)
        {
            lowRangeIn[(size_t) i] = 0.2f * (float) std::sin (lowRangePhase);
            lowRangePhase += 6.2831853071795864769 * lowRangeHz / sr;
        }
        lowRangeEngine.processCorrected (lowRangeIn.data(), lowRangeOut.data(),
                                        block, settings);
    }
    const auto lowPitch = lowRangeEngine.getPitchResult();
    const double lowRangeError = lowPitch.voiced
        ? std::abs (1200.0 * std::log2 (lowPitch.frequencyHz / lowRangeHz)) : 9999.0;

    sattari::dsp::ExtremePitchShifter unityShifter;
    unityShifter.prepare (sr, 512);
    std::vector<float> unity ((size_t) total);
    for (int offset = 0; offset < total; offset += block)
        unityShifter.process (input.data() + offset, unity.data() + offset,
                              std::min (block, total - offset), 1.0, true,
                              sr / sourceHz);
    double unityError = 0.0;
    for (int i = unityShifter.getLatencySamples(); i < total; ++i)
        unityError = std::max (unityError,
            std::abs ((double) unity[(size_t) i]
                    - (double) input[(size_t) (i - unityShifter.getLatencySamples())]));
    std::printf ("  blockDelta=%.3f ct energyRatio=%.4f boundaryJump=%.5f unityError=%.9f\n",
                 blockPitchDelta, blockEnergyRatio, boundaryJump, unityError);
    std::printf ("  acquisition voiced=%.2f ms correction=%.2f ms\n",
                 voicedMs, correctedMs);
    std::printf ("  low-range E2 tracker error=%.2f ct voiced=%s\n",
                 lowRangeError, lowPitch.voiced ? "yes" : "no");
    const bool ok = shifter.getLatencySamples() <= 110
                 && std::abs (cents) < 20.0
                 && targetPower > 4.0 * sourcePower
                 && blockPitchDelta < 2.0
                 && blockEnergyRatio > 0.98 && blockEnergyRatio < 1.02
                 && boundaryJump < 0.05
                 && voicedAt > 0 && correctedAt > 0
                 && voicedMs <= 30.0 && correctedMs <= 45.0
                 && lowPitch.voiced && lowRangeError < 10.0
                 && unityError < 1.0e-6;
    std::printf (ok ? "extreme live pitch contract ok\n"
                    : "extreme live pitch contract FAILED\n");
    return ok ? 0 : 1;
}
