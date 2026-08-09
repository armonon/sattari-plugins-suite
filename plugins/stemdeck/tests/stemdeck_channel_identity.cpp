#include "MainPluginProcessor.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& description)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << description << '\n';
    if (! ok)
        ++failures;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    std::cout << "Channel identity and live-input routing contract\n";

    MainPluginProcessor processor;
    // A host normally negotiates this before prepareToPlay. The synthetic contract
    // must do the same or the processor quite correctly sees no device input.
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    const int inputIndex = processor.addChannel (ChannelType::input);
    check (inputIndex >= 0, "a typed live-input channel can be added");
    check (processor.getChannelId (inputIndex) == 4,
           "the first dynamic channel receives the next permanent ID");
    check (processor.getChannelSlot (inputIndex) == 4,
           "the first dynamic channel owns the first free parameter/FX slot");

    processor.setInputMonitorDispatched (inputIndex, true);
    juce::AudioBuffer<float> audio (2, blockSize);
    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto value = (float) (0.05 * std::sin (
            juce::MathConstants<double>::twoPi * 997.0
            * (double) sample / sampleRate));
        audio.setSample (0, sample, value);
        audio.setSample (1, sample, value);
    }
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    const float monitoredPeak = juce::jmax (
        audio.getMagnitude (0, 0, blockSize),
        audio.getMagnitude (1, 0, blockSize));
    std::cout << "  ..   monitored peak " << monitoredPeak << '\n';
    check (monitoredPeak > 0.035f && monitoredPeak < 0.075f,
           "typed input reaches the monitor exactly once (no legacy-mic doubling)");

    processor.setInputMonitorDispatched (inputIndex, false);
    for (int sample = 0; sample < blockSize; ++sample)
    {
        const auto value = (float) (0.05 * std::sin (
            juce::MathConstants<double>::twoPi * 997.0
            * (double) (sample + blockSize) / sampleRate));
        audio.setSample (0, sample, value);
        audio.setSample (1, sample, value);
    }
    midi.clear();
    processor.processBlock (audio, midi);
    check (audio.getMagnitude (0, 0, blockSize) < 1.0e-6f
               && audio.getMagnitude (1, 0, blockSize) < 1.0e-6f,
           "monitor-off silences only the live mix path");
    const float monitorOffActivity =
        processor.getChannelActivity (inputIndex);
    std::cout << "  ..   monitor-off activity "
              << monitorOffActivity << '\n';
    check (monitorOffActivity > 0.035f,
           "monitor-off input still runs through capture and metering");

    check (processor.removeChannel (1), "a channel can be removed from the middle");
    const int instrumentIndex = processor.addChannel (ChannelType::midiInstrument);
    check (instrumentIndex >= 0, "an instrument channel can be added after removal");
    check (processor.getChannelId (instrumentIndex) == 5,
           "removed channel IDs are never reused");
    check (processor.getChannelSlot (instrumentIndex) == 1,
           "a free processing slot is reused without colliding with another channel");

    std::vector<ChannelID> ids;
    std::vector<int> slots;
    for (int i = 0; i < processor.getNumChannels(); ++i)
    {
        ids.push_back (processor.getChannelId (i));
        slots.push_back (processor.getChannelSlot (i));
    }

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    MainPluginProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    check (restored.getNumChannels() == processor.getNumChannels(),
           "state restore preserves the dynamic channel count");
    for (int i = 0; i < restored.getNumChannels(); ++i)
    {
        check (restored.getChannelId (i) == ids[(size_t) i],
               "state restore preserves permanent channel ID " + juce::String (ids[(size_t) i]));
        check (restored.getChannelSlot (i) == slots[(size_t) i],
               "state restore preserves processing slot " + juce::String (slots[(size_t) i]));
    }

    const int next = restored.addChannel (ChannelType::input);
    check (next >= 0 && restored.getChannelId (next) == 6,
           "ID allocation continues monotonically after state restore");
    check (restored.addChannel (ChannelType::sampler) < 0
               && restored.addChannel (ChannelType::pluginHost) < 0,
           "reserved channel kinds fail explicitly until they have real bodies");

    // Editable lane volume is evaluated by the audio schedule itself, not a UI
    // timer. A 0→1 ramp must be audible within one render block.
    auto rampSource =
        std::make_shared<juce::AudioBuffer<float>> (2, 128);
    for (int ch = 0; ch < 2; ++ch)
        for (int sample = 0; sample < 128; ++sample)
            rampSource->setSample (ch, sample, 1.0f);
    ArrangementPlayer::Clip rampClip;
    rampClip.audio = rampSource;
    rampClip.sourceSampleRate = 128.0;
    rampClip.timelineLengthSamples = 127;
    rampClip.gainAutomation = { { 0, 0.0f }, { 126, 1.0f } };
    auto schedule = std::make_shared<ArrangementPlayer::Schedule>();
    schedule->push_back (std::move (rampClip));
    ArrangementPlayer arrangement;
    arrangement.prepare (128.0, 128);
    arrangement.setSchedule (schedule);
    auto masterAutomation =
        std::make_shared<ArrangementPlayer::MasterAutomation>();
    masterAutomation->gainDb = { { 0, -12.0f }, { 126, 0.0f } };
    masterAutomation->smartOn = { { 0, 0.0f }, { 64, 1.0f } };
    arrangement.setMasterAutomation (masterAutomation);
    arrangement.start();
    juce::AudioBuffer<float> rampOut (2, 127);
    rampOut.clear();
    arrangement.process (rampOut);
    check (rampOut.getSample (0, 10) < 0.15f
               && rampOut.getSample (0, 110) > 0.80f,
           "arrangement volume automation changes audio sample-accurately");
    const auto masterAtStart = arrangement.getCurrentMasterState();
    check (masterAtStart.has (ArrangementPlayer::MasterState::gainPresent)
               && std::abs (masterAtStart.gainDb + 12.0f) < 0.001f,
           "arrangement publishes captured master gain automation");
    check (masterAtStart.has (ArrangementPlayer::MasterState::smartOnPresent)
               && masterAtStart.smartOn < 0.5f,
           "arrangement uses stepped state for captured master toggles");

    arrangement.setPositionSeconds (0.5);   // sample 64 at 128 Hz
    arrangement.start();
    juce::AudioBuffer<float> laterOut (2, 1);
    laterOut.clear();
    arrangement.process (laterOut);
    const auto masterLater = arrangement.getCurrentMasterState();
    check (masterLater.gainDb > -6.1f && masterLater.gainDb < -5.8f,
           "captured master gain interpolates at arrangement sample time");
    check (masterLater.smartOn > 0.5f,
           "captured master toggle changes exactly at its event sample");

    auto filterSource =
        std::make_shared<juce::AudioBuffer<float>> (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int sample = 0; sample < 512; ++sample)
            filterSource->setSample (
                ch, sample, (sample & 1) == 0 ? 1.0f : -1.0f);
    ArrangementPlayer::Clip filterClip;
    filterClip.audio = filterSource;
    filterClip.sourceSampleRate = 48000.0;
    filterClip.timelineLengthSamples = 511;
    filterClip.filterAutomation = { { 0, 1.0f } };
    filterClip.dspEnabled = true;
    auto filterSchedule =
        std::make_shared<ArrangementPlayer::Schedule>();
    filterSchedule->push_back (std::move (filterClip));
    ArrangementPlayer filteredArrangement;
    filteredArrangement.prepare (48000.0, 512);
    filteredArrangement.setSchedule (filterSchedule);
    filteredArrangement.start();
    juce::AudioBuffer<float> filteredOut (2, 511);
    filteredOut.clear();
    filteredArrangement.process (filteredOut);
    check (filteredOut.getRMSLevel (0, 128, 300) < 0.15f,
           "captured filter automation uses the realtime deck DSP in arrangement playback");

    MainPluginProcessor packageSource;
    const juce::String oldPath ("/old-machine/Music/source.wav");
    const juce::String movedPath (
        "/moved-session/Media/source.wav");
    packageSource.parameters().state.setProperty (
        "deckAFile", oldPath, nullptr);
    juce::MemoryBlock packageState;
    packageSource.getStateInformation (packageState);
    juce::StringPairArray packageMap;
    packageMap.set (oldPath, movedPath);
    check (packageSource.remapStateMediaPaths (
               packageState, packageMap),
           "portable package rewrites opaque processor media state");
    MainPluginProcessor packageRestored;
    packageRestored.setStateInformation (
        packageState.getData(), (int) packageState.getSize());
    check (packageRestored.parameters().state
               .getProperty ("deckAFile").toString() == movedPath,
           "moved package resolves its mapped media path");

    processor.releaseResources();
    restored.releaseResources();
    std::cout << (failures == 0 ? "CHANNEL_IDENTITY_OK\n" : "CHANNEL_IDENTITY_FAILED\n");
    return failures == 0 ? 0 : 1;
}
