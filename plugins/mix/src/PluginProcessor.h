#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariAudioCore.h>
#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/modules/TransientModule.h>

#include <memory>
#include <vector>

// =============================================================================
//  Sattari Mix — the mixing/dynamics rack.
// -----------------------------------------------------------------------------
//  One plugin that hosts the suite's mix modules in a rack of reorderable slots.
//  Each slot picks a module (or Empty) and has its own bypass, so a channel strip
//  is assembled rather than fixed.
//
//  Design notes that matter:
//
//  * EVERY module's parameters exist all the time, namespaced "<id>." — they are
//    NOT created per slot. A plugin's parameter set must be fixed at construction
//    (hosts index automation by it), so the rack decides only WHETHER and in what
//    ORDER a module runs, never which parameters exist.
//  * EVERY module is prepared in prepareToPlay, not just the ones currently in
//    slots, because the user can rewire the rack at any moment and the audio
//    thread must never be the thing that allocates.
//  * Latency is the sum of the active slots and is republished when the rack
//    changes, so delay compensation stays correct as modules are added/removed.
//  * The modules are the SAME implementations the standalone plugins wrap, so
//    Mix and the individual plugins can never drift apart.
// =============================================================================
class SattariMixAudioProcessor final : public juce::AudioProcessor,
                                       private juce::AsyncUpdater
{
public:
    static constexpr int kNumSlots = 8;

    SattariMixAudioProcessor();
    ~SattariMixAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void reset() override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sattari Mix"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    // Rack introspection for the editor.
    int numModules() const noexcept { return (int) modules.size(); }
    juce::StringArray moduleMenuNames() const;                 // "Empty" + display names
    sattari::dsp::SattariModule* moduleAt (int index) noexcept;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String slotParamId (int slot)       { return "slot" + juce::String (slot + 1); }
    static juce::String slotBypassParamId (int slot) { return "slot" + juce::String (slot + 1) + "bypass"; }
    int  computeLatency() const;
    void handleAsyncUpdate() override;   // publishes latency off the audio thread

    // DECLARED BEFORE apvts on purpose: createLayout() populates this roster, and it is
    // called while apvts is being constructed — so the vector must already exist.
    // Index 0 in a slot menu means Empty, so a slot value of N refers to modules[N - 1].
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> modules;

    juce::AudioProcessorValueTreeState apvts;

    double sr_ = 44100.0;
    // Latency changes when the rack is rewired. processBlock only ever compares and
    // flags; setLatencySamples() is called on the message thread via AsyncUpdater,
    // because notifying the host is not realtime-safe.
    std::atomic<int> pendingLatency_ { 0 };
    int reportedLatency_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMixAudioProcessor)
};
