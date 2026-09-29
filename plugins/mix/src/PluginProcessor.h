#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackProcessor.h>
#include "MixAssistDSP.h"
#include "MixSession.h"

class MixController;

// =============================================================================
//  Sattari Mix — the mixing/dynamics rack.
// -----------------------------------------------------------------------------
//  A channel strip you assemble: 8 reorderable slots, each picking one of the
//  suite's mix modules (or Empty) with its own bypass.
//
//  The rack machinery lives in SattariRackProcessor. Mix adds post-rack analysis,
//  linked-session control, and a separate automatable gain/EQ/dynamics stage.
//
//  The modules are the SAME implementations the standalone plugins wrap, so Mix
//  and the individual plugins can never drift apart.
// =============================================================================
class SattariMixAudioProcessor final : public sattari::rack::SattariRackProcessor,
                                     private juce::Timer
{
public:
    SattariMixAudioProcessor();
    ~SattariMixAudioProcessor() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    void prepareToPlay (double, int) override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void updateTrackProperties (const TrackProperties&) override;

    using Settings = sattari::automix::Settings;
    using Track = sattari::automix::Track;
    Settings assistSettings() const noexcept;
    void setAssistSettings (const Settings&);
    Track trackSnapshot() const;
    juce::String groupName() const;
    juce::String trackName() const;
    void setIdentity (const juce::String& group, const juce::String& name);
    void requestAnalysis (const juce::String& capture, int seconds);
    void syncSession();
    std::vector<Track> linkedTracks() const { return session.tracks(); }
    bool sendCommand (const juce::String& target, const juce::var& command) { return session.send (target, command); }
    juce::String instanceId() const { return session.id; }
    juce::String sessionError() const { return transportError; }
    MixController& mixController() { return *controller; }

private:
    void registerFactoryPresets();
    void timerCallback() override { syncSession(); }
    static void addAssistParameters (sattari::dsp::SattariModule::ParamList&);
    std::uint64_t rackFingerprint() const;

    sattari::automix::Session session;
    std::unique_ptr<MixController> controller;
    sattari::automix::Analyzer analyzer;
    sattari::automix::AssistDSP assist;
    std::array<std::atomic<float>*, 9> controls {};
    std::atomic<float> monitorTrim { 0 };
    std::atomic<unsigned> requestedCapture { 0 }, publishedCapture { 0 };
    std::atomic<int> captureSeconds { 0 };
    unsigned audioCapture = 0;
    int rackParameterCount = 0;
    std::atomic<std::uint64_t> capturedRack { 0 };
    std::atomic<bool> captureChanged { false };
    juce::String captureId, lastCommand, commandError, trimOwner, transportError;
    mutable juce::CriticalSection identityMutex;
    juce::String group, label;
    bool customLabel = false;
    std::atomic<bool> joinPending { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMixAudioProcessor)
};
