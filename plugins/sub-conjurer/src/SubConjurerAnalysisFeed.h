#pragma once

#include <algorithm>

namespace sattari::subconjurer
{
// Input pointers remain readable throughout this synchronous feed. Detector must
// copy/consume each chunk before returning (PitchDetector::pushBlock does so).
// No scratch growth occurs here. Empty scratch uses one stack sample.
template <typename Detector>
void feedPitchInput (const float* const* inputs, int channels, int samples,
                     float* scratch, int scratchCapacity, Detector& detector)
{
    float fallback = 0.0f;
    if (scratch == nullptr || scratchCapacity <= 0)
    {
        scratch = &fallback;
        scratchCapacity = 1;
    }

    for (int offset = 0; offset < samples;)
    {
        const int chunk = std::min (samples - offset, scratchCapacity);
        std::fill (scratch, scratch + chunk, 0.0f);
        for (int channel = 0; channel < channels; ++channel)
            for (int i = 0; i < chunk; ++i)
                scratch[i] += inputs[channel][offset + i] / (float) channels;
        detector.pushBlock (scratch, chunk);
        offset += chunk;
    }
}
}
