#include "TransportSync.h"

TransportSync::Snapshot TransportSync::update (juce::AudioPlayHead* playHead)
{
    Snapshot next = lastSnapshot;

    if (playHead != nullptr)
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
                next.bpm = *bpm;
            if (auto ppq = position->getPpqPosition())
                next.ppqPosition = *ppq;
            next.hostPlaying = position->getIsPlaying();
        }
    }

    // Seqlock publish: bump to odd, write, bump to even. A concurrent reader that
    // observes an odd or changed counter retries, so it never sees a torn field.
    const auto s = seq.load (std::memory_order_relaxed);
    seq.store (s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    lastSnapshot = next;
    std::atomic_thread_fence (std::memory_order_release);
    seq.store (s + 2, std::memory_order_relaxed);

    return next;
}

TransportSync::Snapshot TransportSync::getSnapshot() const
{
    for (;;)
    {
        const auto before = seq.load (std::memory_order_relaxed);
        if (before & 1u)            // writer in progress — spin briefly
            continue;
        std::atomic_thread_fence (std::memory_order_acquire);
        Snapshot copy = lastSnapshot;
        std::atomic_thread_fence (std::memory_order_acquire);
        if (seq.load (std::memory_order_relaxed) == before)
            return copy;            // stable read, no write intervened
    }
}
