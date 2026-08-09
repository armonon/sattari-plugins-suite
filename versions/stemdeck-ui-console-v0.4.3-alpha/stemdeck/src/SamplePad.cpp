#include "SamplePad.h"

void SamplePad::prepare (double sampleRate)
{
    currentSampleRate = sampleRate;
    playheadSamples = 0.0;
    resetRequested.store (false);
    active.store (false);
}

void SamplePad::setLoadedAudio (std::shared_ptr<const DeckPlayer::LoadedAudio> newAudio)
{
    std::atomic_store (&loadedAudio, std::move (newAudio));
    resetRequested.store (true);   // audio thread rewinds the playhead
    active.store (false);
}

std::shared_ptr<const DeckPlayer::LoadedAudio> SamplePad::getLoadedAudio() const
{
    return std::atomic_load (&loadedAudio);
}

juce::String SamplePad::getPath() const
{
    auto audio = getLoadedAudio();
    return audio ? audio->path : juce::String{};
}

void SamplePad::trigger()
{
    resetRequested.store (true);   // rewind happens on the audio thread
    active.store (true);
}

void SamplePad::stop()
{
    active.store (false);
}

void SamplePad::process (juce::AudioBuffer<float>& destination, int startSample, int numSamples, float gainDb)
{
    if (resetRequested.exchange (false))
        playheadSamples = 0.0;

    if (! active.load())
        return;

    auto audio = getLoadedAudio();
    if (! audio || audio->audio.getNumSamples() <= 1)
    {
        active.store (false);
        return;
    }

    const float gain = juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 6.0f, gainDb));
    const double ratio = audio->sourceSampleRate / currentSampleRate;
    const int sourceLength = audio->audio.getNumSamples();

    for (int i = 0; i < numSamples; ++i)
    {
        if (playheadSamples >= (double) sourceLength)
        {
            active.store (false);
            break;
        }

        const int pos = juce::jlimit (0, sourceLength - 1, (int) playheadSamples);
        const float left = audio->audio.getSample (0, pos) * gain;
        const float right = audio->audio.getSample (audio->audio.getNumChannels() > 1 ? 1 : 0, pos) * gain;
        const int outIndex = startSample + i;

        if (destination.getNumChannels() == 1)
        {
            destination.addSample (0, outIndex, 0.5f * (left + right));
        }
        else
        {
            destination.addSample (0, outIndex, left);
            destination.addSample (1, outIndex, right);
            for (int ch = 2; ch < destination.getNumChannels(); ++ch)
                destination.addSample (ch, outIndex, 0.5f * (left + right));
        }

        playheadSamples += ratio;
    }
}
