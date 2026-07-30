#include "ChunkedAudioFile.h"
#include <iostream>
#include <memory>

int main()
{
    constexpr int sampleRate = 8000;
    constexpr int seconds = 40;
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("stemdeck-long-audio-contract.wav");
    file.deleteFile();

    juce::AudioBuffer<float> source (2, sampleRate * seconds);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < source.getNumSamples(); ++i)
            source.setSample (ch, i, ch == 0 ? 0.25f : -0.25f);

    {
        juce::WavAudioFormat wav;
        auto os = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            wav.createWriterFor (os.get(), sampleRate, 2, 24, {}, 0));
        if (writer == nullptr) return 1;
        os.release();
        if (! writer->writeFromAudioSampleBuffer (source, 0, source.getNumSamples()))
            return 1;
    }

    auto streamed = ChunkedAudioFile::create (file);
    const auto firstWindow = streamed != nullptr
                                 ? streamed->acquireReadView (100.25)
                                 : ChunkedAudioFile::ReadView {};
    bool ok = streamed != nullptr
           && streamed->lengthInSamples() == source.getNumSamples()
           && std::abs (streamed->readInterpolated (0, 100.25) - 0.25f) < 1.0e-3f;

    const double distant = sampleRate * 38.0;
    streamed->readInterpolated (0, distant);
    for (int tries = 0; tries < 100
         && std::abs (streamed->readInterpolated (0, distant) - 0.25f) >= 1.0e-3f; ++tries)
        juce::Thread::sleep (10);

    ok = std::abs (streamed->readInterpolated (0, distant) - 0.25f) < 1.0e-3f && ok;
    // A render-block view remains valid after the worker publishes a distant
    // window; reclamation is deferred to the worker thread.
    ok = std::abs (ChunkedAudioFile::readInterpolated (
                       firstWindow, 0, 100.25) - 0.25f) < 1.0e-3f
         && ok;
    const auto fullBytes = (std::size_t) source.getNumChannels()
                         * (std::size_t) source.getNumSamples() * sizeof (float);
    ok = streamed->residentBytes() < fullBytes && ok;

    streamed.reset();
    file.deleteFile();
    std::cout << (ok ? "LONG_AUDIO_STREAM_OK" : "LONG_AUDIO_STREAM_FAILED") << '\n';
    return ok ? 0 : 1;
}
