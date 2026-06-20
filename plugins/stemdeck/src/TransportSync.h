#pragma once

#include <JuceHeader.h>

class TransportSync
{
public:
    struct Snapshot
    {
        double bpm = 120.0;
        bool hostPlaying = false;
        double ppqPosition = 0.0;
    };

    Snapshot update (juce::AudioPlayHead* playHead);
    Snapshot getSnapshot() const { return lastSnapshot; }

private:
    Snapshot lastSnapshot;
};
