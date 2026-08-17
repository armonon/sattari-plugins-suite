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

    // Future channel banks are intentionally host-visible before a channel owns
    // them.  They must remain independent: every unused slot has the sentinel
    // channel id -1, which previously caused a write to Deck F to echo into the
    // first unused bank (Deck E) through the event dispatcher.
    auto* unusedE = processor.parameters().getParameter ("deckEVolume");
    auto* unusedF = processor.parameters().getParameter ("deckFVolume");
    check (unusedE != nullptr && unusedF != nullptr,
           "future channel parameter banks are host-visible");
    if (unusedE != nullptr && unusedF != nullptr)
    {
        unusedE->setValueNotifyingHost (unusedE->convertTo0to1 (-12.0f));
        unusedF->setValueNotifyingHost (unusedF->convertTo0to1 (6.0f));
        check (std::abs (processor.parameters().getRawParameterValue ("deckEVolume")->load()
                         - (-12.0f)) < 0.01f
                   && std::abs (processor.parameters().getRawParameterValue ("deckFVolume")->load()
                                - 6.0f) < 0.01f,
               "automation in unused channel banks cannot cross-route");
    }

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

    // Perform-dock state is host-visible and round-trippable: arbitrary endpoints
    // plus four real pre-master stem buses (0/100/300 gain, mute, solo).
    auto* transitionFrom = processor.parameters().getParameter ("transitionFrom");
    auto* transitionTo = processor.parameters().getParameter ("transitionTo");
    auto* vocalGain = processor.parameters().getParameter ("masterStemVocalsGain");
    auto* vocalSolo = processor.parameters().getParameter ("masterStemVocalsSolo");
    check (transitionFrom != nullptr && transitionTo != nullptr
               && vocalGain != nullptr && vocalSolo != nullptr,
           "transition and master-stem controls are host-visible parameters");
    auto* deckGain = processor.parameters().getParameter ("deckAVolume");
    auto* deckLow = processor.parameters().getParameter ("deckALow");
    check (deckGain != nullptr && deckLow != nullptr,
           "track gain and EQ remain host-visible parameters");
    if (deckGain != nullptr && deckLow != nullptr && vocalGain != nullptr)
    {
        check (std::abs (deckGain->convertTo0to1 (0.0f) - 0.5f) < 0.002f
                   && std::abs (deckLow->convertTo0to1 (0.0f) - 0.5f) < 0.002f,
               "100% unity is physically centred for track gain and EQ");
        check (std::abs (vocalGain->getNormalisableRange().end - 300.0f) < 0.01f
                   && std::abs (vocalGain->convertTo0to1 (100.0f) - 0.5f) < 0.002f,
               "stem gain spans 0..300% with 100% physically centred");
    }
    if (transitionFrom != nullptr && transitionTo != nullptr
        && vocalGain != nullptr && vocalSolo != nullptr)
    {
        transitionFrom->setValueNotifyingHost (transitionFrom->convertTo0to1 (2.0f));
        transitionTo->setValueNotifyingHost (transitionTo->convertTo0to1 (0.0f));
        vocalGain->setValueNotifyingHost (vocalGain->convertTo0to1 (137.0f));
        vocalSolo->setValueNotifyingHost (1.0f);
    }

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

    // Persist a deliberately unsafe live state. Loading a project/preset must keep
    // its routing and media while requiring a fresh gesture before sound starts.
    if (auto* play = processor.parameters().getParameter ("deckAPlay"))
        play->setValueNotifyingHost (1.0f);
    if (auto* monitor = processor.parameters().getParameter ("micMonitor"))
        monitor->setValueNotifyingHost (1.0f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);
    MainPluginProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    check (restored.parameters().getRawParameterValue ("deckAPlay")->load() < 0.5f,
           "state restore never auto-starts a saved deck transport");
    check (restored.parameters().getRawParameterValue ("micMonitor")->load() < 0.5f,
           "state restore never auto-enables microphone monitoring");
    check (std::abs (restored.parameters().getRawParameterValue ("transitionFrom")->load() - 2.0f) < 0.01f
               && std::abs (restored.parameters().getRawParameterValue ("transitionTo")->load()) < 0.01f,
           "state restore preserves arbitrary transition endpoints");
    check (std::abs (restored.parameters().getRawParameterValue ("masterStemVocalsGain")->load() - 137.0f) < 0.01f
               && restored.parameters().getRawParameterValue ("masterStemVocalsSolo")->load() > 0.5f,
           "state restore preserves master-stem gain and solo intent");
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

    // A loop boundary may land anywhere inside the host buffer. Playback must
    // fill the remainder from loopStart immediately; wrapping only after the
    // block leaks audio beyond loopEnd and makes short arranger loops stumble.
    auto loopSource =
        std::make_shared<juce::AudioBuffer<float>> (2, 16);
    for (int ch = 0; ch < 2; ++ch)
        for (int sample = 0; sample < 16; ++sample)
            loopSource->setSample (ch, sample, (float) sample / 10.0f);
    ArrangementPlayer::Clip loopClip;
    loopClip.audio = loopSource;
    loopClip.sourceSampleRate = 100.0;
    loopClip.timelineLengthSamples = 15;
    auto loopSchedule =
        std::make_shared<ArrangementPlayer::Schedule>();
    loopSchedule->push_back (std::move (loopClip));
    ArrangementPlayer loopingArrangement;
    loopingArrangement.prepare (100.0, 7);
    loopingArrangement.setSchedule (loopSchedule);
    loopingArrangement.setPositionSeconds (0.03); // sample 3
    loopingArrangement.setLoop (0.02, 0.05, true); // samples [2, 5)
    loopingArrangement.start();
    juce::AudioBuffer<float> loopOut (2, 7);
    loopOut.clear();
    loopingArrangement.process (loopOut);
    const std::array<float, 7> expectedLoop {
        0.3f, 0.4f, 0.2f, 0.3f, 0.4f, 0.2f, 0.3f
    };
    bool loopExact = true;
    for (int sample = 0; sample < loopOut.getNumSamples(); ++sample)
        loopExact = loopExact
                 && std::abs (loopOut.getSample (0, sample)
                              - expectedLoop[(size_t) sample]) < 1.0e-5f;
    check (loopExact,
           "arrangement loops wrap sample-accurately inside a host block");
    check (loopingArrangement.getPositionSamples() == 4,
           "arrangement loop playhead preserves the post-wrap remainder");

    // Printed/live echo must not be cut off at a region's source out-point.
    // At 100 Hz DeckFX's echo tap is 30 samples, safely beyond this one-sample
    // source region and therefore only audible if the explicit tail is rendered.
    auto tailSource =
        std::make_shared<juce::AudioBuffer<float>> (2, 4);
    tailSource->clear();
    tailSource->setSample (0, 0, 1.0f);
    tailSource->setSample (1, 0, 1.0f);
    ArrangementPlayer::Clip tailClip;
    tailClip.audio = tailSource;
    tailClip.sourceSampleRate = 100.0;
    tailClip.timelineLengthSamples = 1;
    tailClip.effectTailSamples = 40;
    tailClip.echoAutomation = { { 0, 1.0f } };
    tailClip.dspEnabled = true;
    auto tailSchedule =
        std::make_shared<ArrangementPlayer::Schedule>();
    tailSchedule->push_back (std::move (tailClip));
    ArrangementPlayer tailArrangement;
    tailArrangement.prepare (100.0, 41);
    tailArrangement.setSchedule (tailSchedule);
    tailArrangement.start();
    juce::AudioBuffer<float> tailOut (2, 41);
    tailOut.clear();
    tailArrangement.process (tailOut);
    check (std::abs (tailArrangement.getLengthSeconds() - 0.41) < 1.0e-6,
           "arrangement duration includes captured clip-effect tails");
    check (tailOut.getMagnitude (0, 2, 39) > 0.2f,
           "captured echo decays after the source region instead of being cut off");

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
