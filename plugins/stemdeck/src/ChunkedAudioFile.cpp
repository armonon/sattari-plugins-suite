#include "ChunkedAudioFile.h"
#include <algorithm>

std::shared_ptr<ChunkedAudioFile> ChunkedAudioFile::create (const juce::File& file)
{
    auto result = std::shared_ptr<ChunkedAudioFile> (new ChunkedAudioFile (file));
    if (! result->initialise())
        return {};
    result->startThread (juce::Thread::Priority::low);
    return result;
}

ChunkedAudioFile::ChunkedAudioFile (juce::File source)
    : Thread ("StemDeck long-audio cache"), file (std::move (source))
{
}

ChunkedAudioFile::~ChunkedAudioFile()
{
    signalThreadShouldExit();
    notify();
    stopThread (5000);
}

bool ChunkedAudioFile::initialise()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 1 || reader->sampleRate <= 0.0)
        return false;

    sourceRate = reader->sampleRate;
    sourceLength = reader->lengthInSamples;
    channels = juce::jlimit (1, 2, (int) reader->numChannels);
    windowSamples = juce::jmax<juce::int64> (4096, (juce::int64) std::llround (sourceRate * 30.0));
    return loadAround (0); // prime before the deck is published
}

bool ChunkedAudioFile::loadAround (juce::int64 position)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const auto half = windowSamples / 2;
    const auto start = juce::jlimit<juce::int64> (
        0, juce::jmax<juce::int64> (0, sourceLength - windowSamples), position - half);
    const int count = (int) juce::jmin<juce::int64> (windowSamples, sourceLength - start);

    auto next = std::make_shared<Cache>();
    next->start = start;
    next->audio.setSize (channels, juce::jmax (1, count), false, false, true);
    if (! reader->read (&next->audio, 0, count, start, true, true))
        return false;

    // Hold the old window on the worker before publishing the replacement.
    // It is reclaimed here after a grace period, never by the audio thread.
    if (auto old = std::atomic_load (&cache))
        retiredCaches.push_back (
            { std::move (old), juce::Time::currentTimeMillis() + 2000 });
    std::atomic_store (&cache, std::shared_ptr<const Cache> (std::move (next)));
    return true;
}

void ChunkedAudioFile::run()
{
    while (! threadShouldExit())
    {
        const auto wanted = requestedPosition.load (std::memory_order_relaxed);
        const auto current = std::atomic_load (&cache);
        const auto margin = juce::jmax<juce::int64> (64, windowSamples / 5);
        const auto end = current != nullptr
            ? current->start + current->audio.getNumSamples() : 0;
        if (current == nullptr
            || (wanted < current->start + margin && current->start > 0)
            || (wanted >= end - margin && end < sourceLength))
            loadAround (wanted);

        const auto now = juce::Time::currentTimeMillis();
        retiredCaches.erase (
            std::remove_if (
                retiredCaches.begin(), retiredCaches.end(),
                [now] (const RetiredCache& retired)
                {
                    return retired.releaseAfterMs <= now;
                }),
            retiredCaches.end());

        wait (20);
    }
}

float ChunkedAudioFile::readInterpolated (int channel, double sourcePosition) const noexcept
{
    return readInterpolated (acquireReadView (sourcePosition), channel,
                             sourcePosition);
}

ChunkedAudioFile::ReadView ChunkedAudioFile::acquireReadView (
    double sourcePosition) const noexcept
{
    const auto target = (juce::int64) std::floor (sourcePosition);
    requestedPosition.store (juce::jlimit<juce::int64> (0, juce::jmax<juce::int64> (0, sourceLength - 1), target),
                             std::memory_order_relaxed);
    const auto current = std::atomic_load (&cache);
    if (current == nullptr)
        return {};
    return { current, &current->audio, current->start };
}

float ChunkedAudioFile::readInterpolated (
    const ReadView& view, int channel, double sourcePosition) noexcept
{
    if (view.audio == nullptr || view.audio->getNumSamples() < 2)
        return 0.0f;

    const double local = sourcePosition - (double) view.start;
    const int i0 = (int) std::floor (local);
    if (i0 < 0 || i0 + 1 >= view.audio->getNumSamples())
        return 0.0f;
    const int ch = juce::jlimit (0, view.audio->getNumChannels() - 1,
                                 channel);
    const float frac = (float) (local - i0);
    const float a = view.audio->getSample (ch, i0);
    return a + frac * (view.audio->getSample (ch, i0 + 1) - a);
}

std::size_t ChunkedAudioFile::residentBytes() const noexcept
{
    const auto current = std::atomic_load (&cache);
    return current == nullptr ? 0u
        : (std::size_t) current->audio.getNumChannels()
          * (std::size_t) current->audio.getNumSamples() * sizeof (float);
}
