#include <sattari_audio_core/SattariTempoEstimator.h>

#include <cmath>
#include <cstdio>
#include <vector>

using sattari::dsp::TempoEstimator;

namespace
{
int failures = 0;

void check (const char* label, bool condition)
{
    if (! condition)
    {
        std::printf ("  FAIL %s\n", label);
        ++failures;
    }
}

std::vector<float> pulseTrain (double bpm, double rate, double seconds,
                               bool addOffbeats = false)
{
    std::vector<float> envelope ((std::size_t) std::round (rate * seconds), 0.0f);
    const double period = rate * 60.0 / bpm;
    for (double beat = 0.0; beat < (double) envelope.size(); beat += period)
    {
        const auto i = (std::size_t) std::round (beat);
        if (i < envelope.size()) envelope[i] = 1.0f;
        if (i + 1 < envelope.size()) envelope[i + 1] = 0.35f;
        if (addOffbeats)
        {
            const auto offbeat = (std::size_t) std::round (beat + period * 0.5);
            if (offbeat < envelope.size()) envelope[offbeat] = 0.38f;
        }
    }
    return envelope;
}
}

int main()
{
    constexpr double rate = 44100.0 / 2048.0;
    for (double bpm : { 60.0, 90.0, 120.0, 128.0, 174.0 })
    {
        const auto result = TempoEstimator::analyse (
            pulseTrain (bpm, rate, 30.0), rate);
        std::printf ("  %.1f BPM fixture -> %.3f BPM (confidence %.3f)%s\n",
                     bpm, result.bpm, result.confidence,
                     result.octaveAmbiguous ? " octave-ambiguous" : "");
        check ("clean pulse train is valid", result.valid);
        check ("clean pulse train is within 2 BPM",
               result.valid && std::abs (result.bpm - bpm) <= 2.0);
    }

    const auto syncopated = TempoEstimator::analyse (
        pulseTrain (126.0, rate, 45.0, true), rate);
    check ("offbeat pulse train is valid", syncopated.valid);
    check ("offbeats do not force double tempo",
           syncopated.valid && std::abs (syncopated.bpm - 126.0) <= 2.0);

    const std::vector<float> silence ((std::size_t) (rate * 30.0), 0.0f);
    check ("silence has no tempo", ! TempoEstimator::analyse (silence, rate).valid);
    check ("short evidence has no tempo",
           ! TempoEstimator::analyse (pulseTrain (120.0, rate, 2.0), rate).valid);

    if (failures == 0) std::printf ("tempo estimator contract: OK\n");
    else std::printf ("tempo estimator contract: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
