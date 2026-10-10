// Contract for the opt-in Sattari Live Pitch Engine alpha.
//
// These are foundation gates, not a declaration of release quality. They lock:
//   - causal pitch accuracy and voiced/silence behavior,
//   - an exact delayed-dry unity path,
//   - the typical-vocal latency budget,
//   - finite, bounded pitch shifting with block-size-stable pitch.

#include "sattari_audio_core/SattariLivePitchEngine.h"
#include "sattari_audio_core/SattariPitchDetector.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
    void setBandpass (double sr, double frequency, double q)
    {
        const double omega = 2.0 * pi * frequency / sr;
        const double alpha = std::sin (omega) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0;
        b1 = 0.0;
        b2 = -alpha / a0;
        a1 = -2.0 * std::cos (omega) / a0;
        a2 = (1.0 - alpha) / a0;
    }
    float process (float input)
    {
        const double output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return (float) output;
    }
};

std::vector<float> makeFormantVoice (double sr, double hz, int samples)
{
    std::vector<float> impulses ((size_t) samples, 0.0f);
    for (double position = 0.0; position < samples; position += sr / hz)
        impulses[(size_t) position] = 1.0f;
    Biquad first, second;
    first.setBandpass (sr, 700.0, 6.0);
    second.setBandpass (sr, 1200.0, 8.0);
    std::vector<float> output ((size_t) samples, 0.0f);
    for (int i = 0; i < samples; ++i)
        output[(size_t) i] = 0.7f * first.process (impulses[(size_t) i])
                           + 0.5f * second.process (impulses[(size_t) i]);
    return output;
}

double formantCentroid (const std::vector<float>& audio, int start, int length, double sr)
{
    const int lowBin = std::max (1, (int) (200.0 * length / sr));
    const int highBin = std::min (length / 2 - 1, (int) (2500.0 * length / sr));
    double weighted = 0.0, magnitudeSum = 0.0;
    for (int bin = lowBin; bin <= highBin; ++bin)
    {
        double real = 0.0, imaginary = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos (2.0 * pi * i / (length - 1));
            const double angle = -2.0 * pi * bin * i / length;
            const double sample = audio[(size_t) (start + i)] * window;
            real += sample * std::cos (angle);
            imaginary += sample * std::sin (angle);
        }
        const double magnitude = std::sqrt (real * real + imaginary * imaginary);
        const double frequency = (double) bin * sr / length;
        weighted += frequency * magnitude;
        magnitudeSum += magnitude;
    }
    return magnitudeSum > 1.0e-12 ? weighted / magnitudeSum : 0.0;
}

std::vector<float> naivePitchResample (const std::vector<float>& input, double ratio)
{
    std::vector<float> output (input.size(), 0.0f);
    for (int i = 0; i < (int) output.size(); ++i)
    {
        const double source = ratio * i;
        const int before = (int) std::floor (source);
        if (before + 1 >= (int) input.size()) break;
        const double fraction = source - before;
        output[(size_t) i] = (float) ((1.0 - fraction) * input[(size_t) before]
                                   + fraction * input[(size_t) (before + 1)]);
    }
    return output;
}

std::vector<float> makeHarmonicVoice (double sr, double hz, int samples)
{
    std::vector<float> audio ((size_t) samples, 0.0f);
    double phase = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        // Harmonic but band-limited enough to resemble a clean sustained vowel.
        const double envelope = std::min (1.0, (double) i / (0.015 * sr));
        double x = 0.0;
        for (int harmonic = 1; harmonic <= 12; ++harmonic)
            x += std::sin ((double) harmonic * phase) / (double) harmonic;
        audio[(size_t) i] = (float) (0.16 * envelope * x);
        phase += 2.0 * pi * hz / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
    }
    return audio;
}

std::vector<float> makeVibratoVoice (double sr, double hz, double depthCents,
                                     double rateHz, int samples)
{
    std::vector<float> audio ((size_t) samples, 0.0f);
    double phase = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        const double cents = depthCents * std::sin (2.0 * pi * rateHz * (double) i / sr);
        const double instantaneousHz = hz * std::pow (2.0, cents / 1200.0);
        double x = 0.0;
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
            x += std::sin ((double) harmonic * phase) / (double) harmonic;
        audio[(size_t) i] = (float) (0.18 * x);
        phase += 2.0 * pi * instantaneousHz / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
    }
    return audio;
}

double centsBetween (double measured, double expected)
{
    return measured > 0.0 && expected > 0.0
        ? 1200.0 * std::log2 (measured / expected) : 1.0e9;
}

double median (std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort (values.begin(), values.end());
    return values[values.size() / 2];
}

double measurePitch (const std::vector<float>& audio, double sr, int start)
{
    sattari::dsp::PitchDetector detector;
    detector.prepare (sr, 80.0f, 1000.0f, 0.045f);
    for (int i = std::max (0, start); i + 256 <= (int) audio.size(); i += 256)
        detector.pushBlock (audio.data() + i, 256);
    const auto result = detector.analyse (0.2f);
    return result.voiced ? result.frequencyHz : 0.0;
}

double pitchDeviationCents (const std::vector<float>& audio, double sr, int start)
{
    sattari::dsp::PitchDetector detector;
    detector.prepare (sr, 80.0f, 1000.0f, 0.045f);
    std::vector<double> midi;
    for (int i = std::max (0, start); i + 256 <= (int) audio.size(); i += 256)
    {
        detector.pushBlock (audio.data() + i, 256);
        const auto result = detector.analyse (0.2f);
        if (result.voiced)
            midi.push_back (69.0 + 12.0 * std::log2 ((double) result.frequencyHz / 440.0));
    }
    if (midi.size() < 8) return 0.0;
    double mean = 0.0;
    for (double value : midi) mean += value;
    mean /= (double) midi.size();
    double variance = 0.0;
    for (double value : midi) variance += (value - mean) * (value - mean);
    return 100.0 * std::sqrt (variance / (double) midi.size());
}

std::vector<float> renderKnownPitch (const std::vector<float>& input, double sr,
                                     double inputHz, double ratio, int block,
                                     int& latency)
{
    sattari::dsp::LiveEpochShifter shifter;
    shifter.prepare (sr, 100.0, 1100.0, 2048);
    latency = shifter.getLatencySamples();
    std::vector<float> output (input.size(), 0.0f);
    for (int start = 0; start < (int) input.size(); start += block)
    {
        const int count = std::min (block, (int) input.size() - start);
        shifter.process (input.data() + start, output.data() + start, count,
                         ratio, sr / inputHz, true);
    }
    return output;
}
} // namespace

int main()
{
    constexpr double sr = 48000.0;
    constexpr int block = 128;
    int failures = 0;

    // 1) Causal tracker: representative sung fundamentals must settle accurately.
    for (double expected : { 100.0, 147.0, 220.0, 440.0, 880.0 })
    {
        const auto input = makeHarmonicVoice (sr, expected, (int) (1.2 * sr));
        sattari::dsp::LivePitchTracker tracker;
        tracker.prepare (sr, 100.0, 1100.0, 256);
        std::vector<double> settled;
        for (int start = 0; start < (int) input.size(); start += block)
        {
            const int count = std::min (block, (int) input.size() - start);
            const auto result = tracker.process (input.data() + start, count);
            if (start > (int) (0.25 * sr) && result.voiced)
                settled.push_back (result.frequencyHz);
        }
        const double measured = median (settled);
        const double error = centsBetween (measured, expected);
        std::printf ("tracker %.1f Hz -> %.3f Hz (%+.2f ct), window=%d hop=%d\n",
                     expected, measured, error,
                     tracker.analysisWindowSamples(), tracker.analysisHopSamples());
        if (settled.empty() || std::abs (error) > 5.0) ++failures;
    }

    // 2) Silence never invents a note.
    {
        std::vector<float> silence ((size_t) sr, 0.0f);
        sattari::dsp::LivePitchTracker tracker;
        tracker.prepare (sr);
        bool invented = false;
        for (int start = 0; start < (int) silence.size(); start += block)
            invented = invented || tracker.process (silence.data() + start, block).voiced;
        std::printf ("silence: %s\n", invented ? "FAIL invented pitch" : "unvoiced");
        if (invented) ++failures;
    }

    // Detector behavior and analysis cost stay sample-rate invariant.
    for (double matrixRate : { 44100.0, 96000.0 })
    {
        const auto input = makeHarmonicVoice (matrixRate, 220.0, (int) (0.8 * matrixRate));
        sattari::dsp::LivePitchTracker tracker;
        tracker.prepare (matrixRate, 100.0, 1100.0, 256);
        sattari::dsp::LivePitchTracker::Result result;
        for (int start = 0; start < (int) input.size(); start += 128)
        {
            const int count = std::min (128, (int) input.size() - start);
            result = tracker.process (input.data() + start, count);
        }
        const double error = centsBetween (result.frequencyHz, 220.0);
        std::printf ("rate %.1f kHz: %.3f Hz (%+.2f ct), window %.2f ms\n",
                     matrixRate / 1000.0, result.frequencyHz, error,
                     1000.0 * tracker.analysisWindowSamples() / matrixRate);
        if (! result.voiced || std::abs (error) > 5.0
            || 1000.0 * tracker.analysisWindowSamples() / matrixRate > 24.0)
            ++failures;
    }

    // Broadband noise must not seize the note state. A few held frames after a
    // preceding voiced section would be allowed; a fresh noise-only run gets none.
    {
        std::vector<float> noise ((size_t) sr, 0.0f);
        uint32_t state = 0x51a77a11u;
        for (float& sample : noise)
        {
            state = state * 1664525u + 1013904223u;
            sample = 0.20f * ((float) ((state >> 8) & 0x00ffffffu) / 8388608.0f - 1.0f);
        }
        sattari::dsp::LivePitchTracker tracker;
        tracker.prepare (sr);
        int voicedBlocks = 0, blocks = 0;
        for (int start = 0; start < (int) noise.size(); start += 128)
        {
            ++blocks;
            if (tracker.process (noise.data() + start, 128).voiced) ++voicedBlocks;
        }
        std::printf ("noise: %d/%d voiced blocks\n", voicedBlocks, blocks);
        if (voicedBlocks > blocks / 100) ++failures;
    }

    // A noisy harmonic voice is still a voice. This locks the lower-confidence
    // path needed by rap, saturated vocals and imperfect rooms while the adjacent
    // broadband-noise test prevents the threshold from becoming permissive.
    {
        auto noisyVoice = makeHarmonicVoice (sr, 220.0, (int) sr);
        uint32_t state = 0x8f31c4abu;
        for (float& sample : noisyVoice)
        {
            state = state * 1664525u + 1013904223u;
            sample += 0.08f * ((float) ((state >> 8) & 0x00ffffffu) / 8388608.0f - 1.0f);
        }
        sattari::dsp::LivePitchTracker tracker;
        tracker.prepare (sr);
        std::vector<double> frequencies;
        int eligible = 0;
        for (int start = 0; start < (int) noisyVoice.size(); start += 128)
        {
            const auto result = tracker.process (noisyVoice.data() + start, 128);
            if (start > (int) (0.25 * sr))
            {
                ++eligible;
                if (result.voiced) frequencies.push_back (result.frequencyHz);
            }
        }
        const double measured = median (frequencies);
        const double coverage = eligible > 0 ? (double) frequencies.size() / eligible : 0.0;
        const double error = centsBetween (measured, 220.0);
        std::printf ("noisy voice: %.0f%% voiced, %.2f ct error\n", 100.0 * coverage, error);
        if (coverage < 0.60 || std::abs (error) > 10.0) ++failures;
    }

    // 3) Unity is not an approximate reconstruction: it is the exact delayed input.
    {
        const auto input = makeHarmonicVoice (sr, 173.0, (int) sr);
        int latency = 0;
        const auto output = renderKnownPitch (input, sr, 173.0, 1.0, 128, latency);
        double maximumError = 0.0;
        for (int i = latency; i < (int) output.size(); ++i)
            maximumError = std::max (maximumError,
                                     std::abs ((double) output[(size_t) i]
                                            - (double) input[(size_t) (i - latency)]));
        const double latencyMs = 1000.0 * latency / sr;
        std::printf ("unity: max error=%.9g, latency=%d samples / %.3f ms\n",
                     maximumError, latency, latencyMs);
        if (maximumError != 0.0 || latencyMs > 15.5) ++failures;
    }

    // 4) A real correction ratio remains finite, bounded and pitch-accurate.
    //    Compare two callback sizes to catch scheduling tied to the host block.
    {
        constexpr double inputHz = 180.0;
        const double ratio = std::pow (2.0, 2.0 / 12.0);
        const double expected = inputHz * ratio;
        const auto input = makeHarmonicVoice (sr, inputHz, (int) (2.0 * sr));
        int latency64 = 0, latency256 = 0;
        const auto out64 = renderKnownPitch (input, sr, inputHz, ratio, 64, latency64);
        const auto out256 = renderKnownPitch (input, sr, inputHz, ratio, 256, latency256);
        const double pitch64 = measurePitch (out64, sr, latency64 + (int) (0.5 * sr));
        const double pitch256 = measurePitch (out256, sr, latency256 + (int) (0.5 * sr));
        const double error64 = centsBetween (pitch64, expected);
        const double error256 = centsBetween (pitch256, expected);
        float peak = 0.0f;
        bool finite = true;
        for (float x : out64)
        {
            finite = finite && std::isfinite (x);
            peak = std::max (peak, std::abs (x));
        }
        std::printf ("shift: target=%.3f Hz, b64=%.3f (%+.2f ct), b256=%.3f (%+.2f ct), peak=%.3f\n",
                     expected, pitch64, error64, pitch256, error256, peak);
        if (! finite || peak > 4.0f || std::abs (error64) > 35.0
            || std::abs (error256) > 35.0
            || std::abs (centsBetween (pitch64, pitch256)) > 5.0)
            ++failures;
    }

    // 5) Musical planner: Hard correction centres a 40-cent-sharp note while the
    //    vibrato setting controls the residual instead of moving the note target.
    {
        sattari::dsp::LiveCorrectionPlanner planner;
        planner.prepare (sr);
        sattari::dsp::LiveCorrectionPlanner::Settings hard;
        hard.retuneMilliseconds = 0.0;
        hard.preserveVibrato = 0.0;
        sattari::dsp::LivePitchTracker::Result pitch;
        pitch.voiced = true;
        pitch.confidence = 0.99f;
        pitch.frequencyHz = 440.0 * std::pow (2.0, 40.0 / 1200.0);
        pitch.periodSamples = sr / pitch.frequencyHz;
        double ratio = 1.0;
        for (int i = 0; i < 80; ++i)
            ratio = planner.update (pitch, 128, hard);
        const double correctedMidi = 69.4 + 12.0 * std::log2 (ratio);
        const double errorCents = 100.0 * (correctedMidi - 69.0);
        std::printf ("planner hard: target=%d, correction=%+.2f ct, output error=%+.2f ct\n",
                     planner.getTargetMidi(), planner.getCorrectionCents(), errorCents);
        if (planner.getTargetMidi() != 69 || std::abs (errorCents) > 1.0)
            ++failures;
    }

    // The note target remains stable at the boundary and changes only after the
    // neighboring note wins by the configured hysteresis margin.
    {
        sattari::dsp::LiveCorrectionPlanner planner;
        planner.prepare (sr);
        sattari::dsp::LiveCorrectionPlanner::Settings settings;
        settings.retuneMilliseconds = 0.0;
        settings.preserveVibrato = 0.0;
        settings.noteHysteresisCents = 12.0;
        sattari::dsp::LivePitchTracker::Result pitch;
        pitch.voiced = true;
        pitch.frequencyHz = 440.0;
        for (int i = 0; i < 10; ++i) (void) planner.update (pitch, 128, settings);
        pitch.frequencyHz = 440.0 * std::pow (2.0, 0.55 / 12.0);
        (void) planner.update (pitch, 128, settings);
        const int heldTarget = planner.getTargetMidi();
        pitch.frequencyHz = 440.0 * std::pow (2.0, 0.57 / 12.0);
        (void) planner.update (pitch, 128, settings);
        const int switchedTarget = planner.getTargetMidi();
        std::printf ("note hysteresis: +55 ct -> %d, +57 ct -> %d\n",
                     heldTarget, switchedTarget);
        if (heldTarget != 69 || switchedTarget != 70) ++failures;
    }

    // 6) Engine-level consonant protection: before any voiced lock, even a full
    //    semitone request returns the exact delayed transient rather than grains.
    {
        sattari::dsp::LivePitchEngine engine;
        engine.prepare (sr, 128);
        const int latency = engine.getLatencySamples();
        std::vector<float> input ((size_t) sr, 0.0f), output ((size_t) sr, 0.0f);
        input[4000] = 0.8f;
        input[4001] = -0.35f;
        for (int start = 0; start < (int) input.size(); start += 128)
            engine.process (input.data() + start, output.data() + start, 128,
                            std::pow (2.0, 1.0 / 12.0));
        double maximumError = 0.0;
        for (int i = latency; i < (int) output.size(); ++i)
            maximumError = std::max (maximumError,
                                     std::abs ((double) output[(size_t) i]
                                            - (double) input[(size_t) (i - latency)]));
        std::printf ("transient protection: max delayed-dry error=%.9g\n", maximumError);
        if (maximumError != 0.0) ++failures;
    }

    // 7) A note leap re-anchors the musical centre. Hard mode must never create
    //    the several-semitone overshoot that a slow vibrato centre would request.
    {
        sattari::dsp::LiveCorrectionPlanner planner;
        planner.prepare (sr);
        sattari::dsp::LiveCorrectionPlanner::Settings hard;
        hard.retuneMilliseconds = 0.0;
        hard.preserveVibrato = 0.0;
        hard.noteHysteresisCents = 12.0;
        sattari::dsp::LivePitchTracker::Result pitch;
        pitch.voiced = true;
        pitch.confidence = 0.99f;
        for (int i = 0; i < 40; ++i)
        {
            pitch.frequencyHz = 440.0;
            pitch.periodSamples = sr / pitch.frequencyHz;
            (void) planner.update (pitch, 128, hard);
        }
        pitch.frequencyHz = 523.2511306; // C5, a three-semitone leap
        pitch.periodSamples = sr / pitch.frequencyHz;
        double maximum = 0.0;
        for (int i = 0; i < 20; ++i)
        {
            (void) planner.update (pitch, 128, hard);
            maximum = std::max (maximum, std::abs (planner.getCorrectionCents()));
        }
        std::printf ("planner leap: target=%d, max correction=%.2f ct\n",
                     planner.getTargetMidi(), maximum);
        if (planner.getTargetMidi() != 72 || maximum > 55.0) ++failures;
    }

    // 8) End-to-end vibrato contract. Natural keeps most intentional modulation;
    //    Hard visibly flattens it. This is measured on rendered audio, not merely
    //    inferred from the correction control.
    {
        const auto input = makeVibratoVoice (sr, 440.0, 40.0, 5.5, (int) (3.0 * sr));
        auto render = [&] (bool hard)
        {
            sattari::dsp::LivePitchEngine engine;
            engine.prepare (sr, 128);
            sattari::dsp::LiveCorrectionPlanner::Settings settings;
            settings.retuneMilliseconds = hard ? 0.0 : 24.0;
            settings.preserveVibrato = hard ? 0.0 : 0.82;
            settings.noteHysteresisCents = hard ? 12.0 : 22.0;
            std::vector<float> output (input.size(), 0.0f);
            for (int start = 0; start < (int) input.size(); start += 128)
            {
                const int count = std::min (128, (int) input.size() - start);
                engine.processCorrected (input.data() + start, output.data() + start,
                                         count, settings);
            }
            return std::make_pair (std::move (output), engine.getLatencySamples());
        };
        auto natural = render (false);
        auto hard = render (true);
        const double inputDepth = pitchDeviationCents (input, sr, (int) sr);
        const double naturalDepth = pitchDeviationCents (natural.first, sr,
                                                          natural.second + (int) sr);
        const double hardDepth = pitchDeviationCents (hard.first, sr,
                                                       hard.second + (int) sr);
        std::printf ("vibrato: input %.2f ct, natural %.2f ct (%.0f%%), hard %.2f ct (%.0f%%)\n",
                     inputDepth, naturalDepth, 100.0 * naturalDepth / inputDepth,
                     hardDepth, 100.0 * hardDepth / inputDepth);
        if (inputDepth <= 5.0 || naturalDepth < 0.60 * inputDepth
            || naturalDepth > 1.20 * inputDepth || hardDepth > 0.35 * inputDepth)
            ++failures;
    }

    // 9) Real-time budget at the hardest supported callback in the initial target
    //    matrix. p95 includes detector frames instead of hiding them in an average.
    {
        constexpr int cpuBlock = 64;
        const auto input = makeHarmonicVoice (sr, 173.0, (int) (8.0 * sr));
        std::vector<float> output (input.size(), 0.0f);
        sattari::dsp::LivePitchEngine engine;
        engine.prepare (sr, cpuBlock);
        sattari::dsp::LiveCorrectionPlanner::Settings settings;
        settings.retuneMilliseconds = 12.0;
        settings.preserveVibrato = 0.5;
        std::vector<double> milliseconds;
        milliseconds.reserve (input.size() / cpuBlock);
        double maximumWallMs = 0.0;
       #if defined(_WIN32)
        double measuredCpuMs = 0.0;
       #endif
        auto threadCpuMs = []
        {
           #if defined(_WIN32)
            FILETIME created {}, exited {}, kernel {}, user {};
            if (! GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user))
            {
                std::fprintf (stderr, "Thread CPU clock unavailable; CPU budget NOT VERIFIED\n");
                std::exit (1);
            }
            const auto millisecondsFor = [] (const FILETIME& time)
            {
                const auto ticks = (static_cast<unsigned long long> (time.dwHighDateTime) << 32)
                                 | time.dwLowDateTime;
                return static_cast<double> (ticks) * 1.0e-4;
            };
            return millisecondsFor (kernel) + millisecondsFor (user);
           #else
            timespec time {};
            if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &time) != 0)
            {
                std::fprintf (stderr, "Thread CPU clock unavailable; CPU budget NOT VERIFIED\n");
                std::exit (1);
            }
            return (double) time.tv_sec * 1000.0 + (double) time.tv_nsec * 1.0e-6;
           #endif
        };
        for (int start = 0; start < (int) input.size(); start += cpuBlock)
        {
            const int count = std::min (cpuBlock, (int) input.size() - start);
            const auto wallBefore = std::chrono::steady_clock::now();
            const auto before = threadCpuMs();
            engine.processCorrected (input.data() + start, output.data() + start,
                                     count, settings);
            const auto after = threadCpuMs();
            const auto wallAfter = std::chrono::steady_clock::now();
            if (start > (int) sr)
            {
               #if defined(_WIN32)
                // Windows CPU accounting may be coarser than a callback. Wall
                // durations conservatively bound CPU percentiles, never replace
                // them with zero-valued CPU samples or converted cycle counts.
                measuredCpuMs += after - before;
                milliseconds.push_back (std::chrono::duration<double, std::milli> (wallAfter - wallBefore).count());
               #else
                milliseconds.push_back (after - before);
               #endif
                maximumWallMs = std::max (maximumWallMs,
                    std::chrono::duration<double, std::milli> (wallAfter - wallBefore).count());
            }
        }
        std::sort (milliseconds.begin(), milliseconds.end());
        const double p95 = milliseconds[(size_t) (0.95 * (double) (milliseconds.size() - 1))];
        const double p99 = milliseconds[(size_t) (0.99 * (double) (milliseconds.size() - 1))];
        const double maximum = milliseconds.back();
        double mean = 0.0;
        for (double value : milliseconds) mean += value;
        mean /= (double) milliseconds.size();
       #if defined(_WIN32)
        std::printf ("Windows measured thread CPU mean %.4f ms; conservative wall bounds 48k/64: mean %.4f ms, p95 %.4f ms, p99 %.4f ms, max %.3f ms\n",
                     measuredCpuMs / (double) milliseconds.size(), mean, p95, p99, maximum);
       #else
        const double callbackMs = 1000.0 * cpuBlock / sr;
        std::printf ("thread CPU 48k/64: mean %.4f ms (%.1f%%), p95 %.4f ms (%.1f%%), p99 %.4f ms (%.1f%%), max %.3f ms; wall max %.3f ms\n",
                     mean, 100.0 * mean / callbackMs, p95, 100.0 * p95 / callbackMs,
                     p99, 100.0 * p99 / callbackMs, maximum, maximumWallMs);
       #endif
        // Keep the CPU thresholds unchanged. Wall time is separately reported:
        // a heavily loaded machine can deschedule a non-real-time test process;
        // a passing CPU gate is not evidence of dropout-free host monitoring.
        // Windows applies the same limits to stricter wall upper bounds.
        if (mean > 0.25 || p95 > 0.25 || p99 > 0.75) ++failures;
    }

    // 10) TD-PSOLA must leave the vocal-tract envelope closer to the source than
    //     ordinary resampling. Raw pitch-synchronous grains provide this without
    //     a separate formant estimator or its latency.
    {
        constexpr double inputHz = 150.0;
        const double ratio = std::pow (2.0, 3.0 / 12.0);
        const auto input = makeFormantVoice (sr, inputHz, (int) sr);
        int latency = 0;
        const auto shifted = renderKnownPitch (input, sr, inputHz, ratio, 128, latency);
        const auto naive = naivePitchResample (input, ratio);
        constexpr int transform = 2048;
        const int inputStart = (int) (0.65 * sr);
        const int shiftedStart = inputStart + latency;
        const double sourceCentroid = formantCentroid (input, inputStart, transform, sr);
        const double shiftedCentroid = formantCentroid (shifted, shiftedStart, transform, sr);
        const double naiveCentroid = formantCentroid (naive, inputStart, transform, sr);
        const double shiftedDistance = std::abs (shiftedCentroid - sourceCentroid);
        const double naiveDistance = std::abs (naiveCentroid - sourceCentroid);
        std::printf ("formant: source %.0f Hz, live %.0f Hz, resample %.0f Hz (live distance %.0f%% of naive)\n",
                     sourceCentroid, shiftedCentroid, naiveCentroid,
                     naiveDistance > 1.0 ? 100.0 * shiftedDistance / naiveDistance : 1000.0);
        if (sourceCentroid <= 0.0 || shiftedDistance >= 0.80 * naiveDistance)
            ++failures;
    }

    std::printf (failures == 0 ? "Sattari Live Pitch alpha contract ok\n"
                                : "Sattari Live Pitch alpha contract FAILED (%d)\n",
                 failures);
    return failures == 0 ? 0 : 1;
}
