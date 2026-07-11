#pragma once

#include <JuceHeader.h>
#include <atomic>

class TransportSync
{
public:
    struct Snapshot
    {
        double bpm = 120.0;
        bool hostPlaying = false;
        double ppqPosition = 0.0;
    };

    // Audio thread: updates the snapshot and publishes it via a seqlock.
    Snapshot update (juce::AudioPlayHead* playHead);

    // Any thread (typically the message thread): tears a consistent copy of the
    // snapshot. The seqlock retry loop guarantees no torn double/bool reads.
    Snapshot getSnapshot() const;

private:
    Snapshot lastSnapshot;                          // written on the audio thread only
    mutable std::atomic<std::uint32_t> seq { 0 };   // even = stable, odd = writing
};
