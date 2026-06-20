#pragma once

// =============================================================================
//  Sattari Audio Suite — shared host transport reader
// -----------------------------------------------------------------------------
//  Wraps the JUCE `getPlayHead() -> getPosition() -> {getBpm, getPpqPosition,
//  getIsPlaying}` boilerplate that every tempo-sync plugin reimplements. One
//  call, one struct, fallbacks already applied (bpm clamped to a musical range,
//  ppq < 0 means "not provided", isPlaying defaults to false).
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>

namespace sattari::host
{

struct TransportSnapshot
{
    double bpm = 120.0;        // host BPM, clamped to [20, 300]; falls back to 120.0
    double ppqPosition = -1.0; // current PPQ; < 0 if the host didn't report one
    bool   isPlaying = false;  // host transport state
    bool   hasHostBpm = false; // true if the host actually reported a BPM
    bool   hasPpq = false;     // true if the host actually reported a PPQ position
};

// Single-call replacement for the per-plugin getPlayHead boilerplate.
inline TransportSnapshot read (juce::AudioProcessor& proc) noexcept
{
    TransportSnapshot s;
    if (auto* ph = proc.getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
            {
                s.bpm = juce::jlimit (20.0, 300.0, *b);
                s.hasHostBpm = true;
            }
            if (auto p = pos->getPpqPosition())
            {
                s.ppqPosition = *p;
                s.hasPpq = true;
            }
            s.isPlaying = pos->getIsPlaying();
        }
    return s;
}

} // namespace sattari::host
