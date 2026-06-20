#include "TransportSync.h"

TransportSync::Snapshot TransportSync::update (juce::AudioPlayHead* playHead)
{
    if (playHead != nullptr)
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                lastSnapshot.bpm = *bpm;
            if (auto ppq = position->getPpqPosition())
                lastSnapshot.ppqPosition = *ppq;
            lastSnapshot.hostPlaying = position->getIsPlaying();
        }
    }

    return lastSnapshot;
}
