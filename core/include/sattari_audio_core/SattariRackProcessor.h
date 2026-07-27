#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariHostTransport.h>

#include <functional>
#include <memory>
#include <vector>

// =============================================================================
//  SattariRackProcessor — the shared rack host.
// -----------------------------------------------------------------------------
//  A rack is one plugin that hosts several SattariModules in reorderable slots:
//  each slot picks a module (or Empty) and has its own bypass, so a chain is
//  assembled rather than fixed. Sattari Mix and Sattari Create are the same
//  machine with different rosters, so the machine lives here — a second copy
//  would drift the moment one of them got a fix.
//
//  A subclass supplies only: the display name, the APVTS state-tree ID, the bus
//  layout, and a factory returning its module roster.
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
//    processBlock only ever compares and flags — setLatencySamples() is called on
//    the message thread via AsyncUpdater, because notifying the host is not
//    realtime-safe.
//  * The transport is read ONCE per block and pushed into every module that runs;
//    a module has no AudioProcessor and so cannot reach getPlayHead() itself.
//  * With an optional second input bus, `buffer` holds BOTH buses back to back,
//    so modules are handed getBusBuffer(buffer, true, 0) — never the raw buffer,
//    which would process the sidechain as audio.
// =============================================================================

namespace sattari::rack
{

class SattariRackProcessor : public juce::AudioProcessor,
                             private juce::AsyncUpdater
{
public:
    static constexpr int kNumSlots = 8;

    using ModuleFactory = std::function<std::vector<std::unique_ptr<dsp::SattariModule>>()>;

    SattariRackProcessor (juce::String displayName,
                          juce::String stateTreeId,
                          const BusesProperties& buses,
                          ModuleFactory factory)
        : juce::AudioProcessor (buses),
          name_ (std::move (displayName)),
          factory_ (std::move (factory)),
          apvts (*this, nullptr, juce::Identifier (stateTreeId), createLayout())
    {
        // Link every module to its namespaced parameters once, off the audio thread.
        for (auto& m : modules)
            m->link (apvts, juce::String (m->moduleId()) + ".");
    }

    ~SattariRackProcessor() override = default;

    // --- AudioProcessor ---------------------------------------------------
    const juce::String getName() const override { return name_; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void releaseResources() override {}

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        sr_ = sampleRate;
        const int n  = juce::jmax (1, samplesPerBlock);
        const int ch = juce::jmax (1, getTotalNumInputChannels());

        // Prepare EVERY module, not just the ones in slots — the rack can be rewired
        // at any moment and the audio thread must never allocate.
        for (auto& m : modules)
            m->prepare (sampleRate, n, ch);

        reportedLatency_ = computeLatency();
        pendingLatency_.store (reportedLatency_, std::memory_order_relaxed);
        setLatencySamples (reportedLatency_);
    }

    void reset() override
    {
        for (auto& m : modules)
            m->reset();
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto o = layouts.getMainOutputChannelSet();
        if (o != juce::AudioChannelSet::stereo() && o != juce::AudioChannelSet::mono())
            return false;
        if (layouts.getMainInputChannelSet() != o)
            return false;
        // The sidechain is optional: disabled, mono or stereo are all fine.
        if (layouts.inputBuses.size() > 1)
        {
            const auto sc = layouts.getChannelSet (true, 1);
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono()
                && sc != juce::AudioChannelSet::stereo())
                return false;
        }
        return true;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;

        // Read the transport ONCE and hand it to every module that is about to run.
        const auto transport = sattari::host::read (*this);

        // Resolve the external key ONCE. Absent or disabled -> nullptr, and every
        // module falls back to keying off the main signal exactly as before.
        const juce::AudioBuffer<float>* sidechain = nullptr;
        juce::AudioBuffer<float> scView;
        if (getBusCount (true) > 1)
            if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
            {
                scView = getBusBuffer (buffer, true, 1);
                if (scView.getNumChannels() > 0 && scView.getNumSamples() > 0)
                    sidechain = &scView;
            }

        // With a second input bus, `buffer` holds BOTH buses back to back.
        auto mainBus = getBusBuffer (buffer, true, 0);

        for (int s = 0; s < kNumSlots; ++s)
        {
            const int sel = (int) apvts.getRawParameterValue (slotParamId (s))->load();
            if (sel <= 0) continue;
            if (apvts.getRawParameterValue (slotBypassParamId (s))->load() > 0.5f) continue;
            if (auto* m = moduleAt (sel - 1))
            {
                m->setTransport (transport);
                m->setSidechain (sidechain);
                m->process (mainBus, midi);
            }
        }

        // Rack rewired? Flag it and let the message thread publish.
        const int lat = computeLatency();
        if (lat != pendingLatency_.load (std::memory_order_relaxed))
        {
            pendingLatency_.store (lat, std::memory_order_relaxed);
            triggerAsyncUpdate();
        }
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    // --- Rack introspection for the editor --------------------------------
    juce::AudioProcessorValueTreeState& parameters() { return apvts; }

    int numModules() const noexcept { return (int) modules.size(); }

    juce::StringArray moduleMenuNames() const
    {
        juce::StringArray menu { "Empty" };
        for (const auto& m : modules) menu.add (m->displayName());
        return menu;
    }

    dsp::SattariModule* moduleAt (int index) noexcept
    {
        return juce::isPositiveAndBelow (index, (int) modules.size())
                 ? modules[(size_t) index].get() : nullptr;
    }

    static juce::String slotParamId (int slot)       { return "slot" + juce::String (slot + 1); }
    static juce::String slotBypassParamId (int slot) { return "slot" + juce::String (slot + 1) + "bypass"; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        // Build the roster first: the layout is derived from it, and the same roster
        // is then kept as the live modules.
        modules = factory_();

        dsp::SattariModule::ParamList p;

        // Slot assignment + bypass. Slot value 0 = Empty, N = modules[N - 1].
        juce::StringArray menu { "Empty" };
        for (auto& m : modules) menu.add (m->displayName());
        for (int s = 0; s < kNumSlots; ++s)
        {
            p.push_back (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { slotParamId (s), 1 }, "Slot " + juce::String (s + 1), menu, 0));
            p.push_back (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { slotBypassParamId (s), 1 }, "Slot " + juce::String (s + 1) + " Bypass", false));
        }

        // Every module's parameters, always present and namespaced.
        for (auto& m : modules)
            m->addParameters (p, juce::String (m->moduleId()) + ".");

        return { p.begin(), p.end() };
    }

    int computeLatency() const
    {
        int total = 0;
        for (int s = 0; s < kNumSlots; ++s)
        {
            const int sel = (int) apvts.getRawParameterValue (slotParamId (s))->load();
            const bool byp = apvts.getRawParameterValue (slotBypassParamId (s))->load() > 0.5f;
            if (sel <= 0 || byp) continue;
            if (auto* m = const_cast<SattariRackProcessor*> (this)->moduleAt (sel - 1))
                total += m->latencySamples();
        }
        return total;
    }

    void handleAsyncUpdate() override
    {
        const int want = pendingLatency_.load (std::memory_order_relaxed);
        if (want != reportedLatency_)
        {
            reportedLatency_ = want;
            setLatencySamples (want);
        }
    }

    juce::String name_;
    ModuleFactory factory_;

    // DECLARED BEFORE apvts on purpose: createLayout() populates this roster, and it
    // is called while apvts is being constructed — so the vector must already exist.
    // Index 0 in a slot menu means Empty, so a slot value of N refers to modules[N-1].
    std::vector<std::unique_ptr<dsp::SattariModule>> modules;

protected:
    juce::AudioProcessorValueTreeState apvts;

private:
    double sr_ = 44100.0;
    std::atomic<int> pendingLatency_ { 0 };
    int reportedLatency_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRackProcessor)
};

} // namespace sattari::rack
