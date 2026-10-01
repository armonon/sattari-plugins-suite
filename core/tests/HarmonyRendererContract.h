#pragma once
#include <sattari_audio_core/SattariHarmonyRenderer.h>
#include <cstdio>
#include <ctime>

namespace harmony_render_test
{
inline int run()
{
    using Renderer = sattari::dsp::HarmonyRenderer;
    int failures = 0;
    auto check = [&] (bool ok, const char* label)
    {
        std::printf ("  harmony renderer %-48s %s\n", label, ok ? "PASS" : "FAIL");
        if (! ok) ++failures;
    };
    constexpr double sr = 48000.0, pi = 3.14159265358979323846;
    std::vector<float> input (96000);
    for (size_t i = 0; i < input.size(); ++i)
        for (int h = 1; h <= 40; ++h)
        {
            const double hz = 220.0 * h;
            const double envelope = 0.08 + std::exp (-std::pow ((hz - 700.0) / 220.0, 2))
                                       + 0.7 * std::exp (-std::pow ((hz - 1800.0) / 350.0, 2));
            input[i] += (float) (0.025 * envelope / std::sqrt ((double) h)
                                * std::sin (2.0 * pi * hz * i / sr));
        }
    auto render = [&] (Renderer& renderer, int block, float formant, bool motion, bool four)
    {
        std::vector<float> result (input.size() * 2);
        std::array<Renderer::Voice, 4> voices;
        for (int v = 0; v < 4; ++v)
        {
            voices[(size_t) v].active = four || v == 0;
            voices[(size_t) v].targetMidi = four ? 57 + (v == 0 ? 4 : v == 1 ? 7 : v == 2 ? -5 : -12) : 57;
            voices[(size_t) v].level = 0.3f;
            voices[(size_t) v].formantSemitones = formant;
            voices[(size_t) v].vibratoCents = motion ? 8.0f : 0.0f;
        }
        Renderer::Settings settings;
        settings.formantsEnabled = ! motion;
        settings.humanPitchCents = motion ? 20.0f : 0.0f;
        settings.humanTimingMs = motion ? 8.0f : 0.0f;
        settings.ensemble = motion ? 0.3f : 0.0f;
        settings.lowCutHz = 20.0f;
        settings.highCutHz = 20000.0f;
        std::vector<double> cpuMs;
        cpuMs.reserve (input.size() / (size_t) block + 1);
        auto threadMs = []
        {
           #if defined(CLOCK_THREAD_CPUTIME_ID)
            timespec t {};
            if (clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t) != 0) return -1.0;
            return (double) t.tv_sec * 1000.0 + (double) t.tv_nsec * 1.0e-6;
           #else
            return -1.0; // unavailable; never substitute wall time for CPU
           #endif
        };
        for (int offset = 0; offset < (int) input.size(); offset += block)
        {
            const int n = std::min (block, (int) input.size() - offset);
            const float* channels[] { input.data() + offset };
            const auto start = threadMs();
            renderer.process (channels, 1, result.data() + offset, result.data() + input.size() + offset,
                n, 57.0, sr / 220.0, true, voices, settings);
            const auto end = threadMs();
            if (start >= 0 && end >= start) cpuMs.push_back (end - start);
        }
        if (block == 64 && motion && four && ! cpuMs.empty())
        {
            std::sort (cpuMs.begin(), cpuMs.end());
            std::printf ("    four-voice renderer thread CPU 48k/64 p95=%.4f ms p99=%.4f ms max=%.4f ms (callback 1.3333 ms)\n",
                cpuMs[(size_t) (0.95 * (double) (cpuMs.size() - 1))],
                cpuMs[(size_t) (0.99 * (double) (cpuMs.size() - 1))], cpuMs.back());
        }
        return result;
    };
    auto maxDifference = [] (const auto& a, const auto& b)
    {
        double difference = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            difference = std::max (difference, std::abs ((double) a[i] - b[i]));
        return difference;
    };
    Renderer renderer;
    renderer.prepare (sr, 1024, 70.0, 1100.0);
    const auto small = render (renderer, 64, 0, true, true);
    renderer.reset();
    const auto repeat = render (renderer, 64, 0, true, true);
    check (maxDifference (small, repeat) < 1.0e-7, "reset restores seeded humanisation exactly");
    for (int block : { 17, 256, 1024 })
    {
        renderer.reset();
        const auto other = render (renderer, block, 0, true, true);
        const auto error = maxDifference (small, other);
        std::printf ("    four-voice motion block 64/%d max difference %.9g\n", block, error);
        check (error < 0.001, "four voices + motion independent of host block");
    }
    bool bounded = true;
    for (float sample : small) bounded = bounded && std::isfinite (sample) && std::abs (sample) < 1.0f;
    check (bounded, "four simultaneous voices finite / unclipped");
    auto brightness = [&] (const auto& audio)
    {
        double hf = 0.0, total = 1.0e-20;
        for (size_t i = 24001; i < input.size(); ++i)
        {
            hf += std::pow ((double) audio[i] - audio[i - 1], 2);
            total += (double) audio[i] * audio[i];
        }
        return hf / total;
    };
    auto pitch = [&] (const auto& audio)
    {
        double best = -1.0; int bestLag = 0;
        for (int lag = 200; lag <= 235; ++lag)
        {
            double xy = 0, xx = 1.0e-20, yy = 1.0e-20;
            for (size_t i = 48000; i < 72000; ++i)
            {
                const auto j = i + (size_t) lag;
                xy += audio[i] * audio[j]; xx += audio[i] * audio[i]; yy += audio[j] * audio[j];
            }
            const double correlation = xy / std::sqrt (xx * yy);
            if (correlation > best) { best = correlation; bestLag = lag; }
        }
        return sr / bestLag;
    };
    renderer.reset(); const auto neutral = render (renderer, 256, 0, false, false);
    renderer.reset(); const auto up = render (renderer, 256, 7, false, false);
    renderer.reset(); const auto down = render (renderer, 256, -7, false, false);
    std::printf ("    formant brightness -7/0/+7 = %.5f / %.5f / %.5f; f0 = %.2f / %.2f\n",
        brightness (down), brightness (neutral), brightness (up), pitch (down), pitch (up));
    check (brightness (up) > brightness (neutral) * 1.1
        && brightness (down) < brightness (neutral) * 0.95, "per-voice formant moves envelope both directions");
    check (std::abs (pitch (up) - 220.0) < 2.0 && std::abs (pitch (down) - 220.0) < 2.0,
           "formant does not transpose fundamental");
    check (renderer.getLatencySamples (true) - renderer.getLatencySamples() == 1024,
           "formant latency explicitly exposed for alignment");
    Renderer extreme;
    extreme.prepare (sr, 1024, 70.0, 1100.0, 8192, true);
    std::printf ("    Extreme bank, four voices + motion:\n");
    const auto extremeSmall = render (extreme, 64, 0, true, true);
    for (int block : { 17, 256, 1024 })
    {
        extreme.reset();
        const auto other = render (extreme, block, 0, true, true);
        const auto error = maxDifference (extremeSmall, other);
        std::printf ("    Extreme block 64/%d max difference %.9g\n", block, error);
        check (error < 0.001, "Extreme motion independent of host block");
    }
    return failures;
}
}
