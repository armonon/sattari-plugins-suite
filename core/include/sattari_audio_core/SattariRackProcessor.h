#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariPresetManager.h>

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
                          ModuleFactory factory,
                          juce::String presetProductId = {})
        : juce::AudioProcessor (buses),
          name_ (std::move (displayName)),
          factory_ (std::move (factory)),
          apvts (*this, nullptr, juce::Identifier (stateTreeId), createLayout()),
          presetBank (apvts, presetProductId.isNotEmpty() ? presetProductId : stateTreeId.toLowerCase())
    {
        // Link every module to its namespaced parameters once, off the audio thread.
        for (auto& m : modules)
            m->link (apvts, juce::String (m->moduleId()) + ".");

        // Rack controls are also fixed for the processor's lifetime. Resolve them
        // once so processBlock/latency scans never construct parameter-id Strings.
        for (int s = 0; s < kNumSlots; ++s)
        {
            slotControls_[(size_t) s].selection =
                apvts.getRawParameterValue (slotParamId (s));
            slotControls_[(size_t) s].bypass =
                apvts.getRawParameterValue (slotBypassParamId (s));
            jassert (slotControls_[(size_t) s].selection != nullptr
                     && slotControls_[(size_t) s].bypass != nullptr);
        }
    }

    ~SattariRackProcessor() override = default;

    // --- AudioProcessor ---------------------------------------------------
    const juce::String getName() const override { return name_; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Factory presets. A rack preset is just a wiring: which module sits in which
    // slot, plus the parameters those modules need. That is exactly what the
    // fixed-chain plugins (Bus, Royal Chain, Vocal Master) were — a hard-coded
    // slot order — so they collapse into presets here rather than staying as code.
    int getNumPrograms() override { return juce::jmax (1, presetBank.numFactoryPresets()); }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram (int i) override
    {
        if (i >= 0 && i < presetBank.numFactoryPresets())
        {
            currentProgram_ = i;
            presetBank.loadFactoryPreset (i);
        }
    }
    const juce::String getProgramName (int i) override
    {
        const auto names = presetBank.factoryPresetNames();
        return juce::isPositiveAndBelow (i, names.size()) ? names[i] : juce::String();
    }
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
                // setDataToReferTo, NOT assignment: AudioBuffer::operator= calls
                // setSize(..., avoidReallocating=false), so it REALLOCATES AND COPIES
                // on every block — an allocation on the audio thread. This re-points
                // at the host's channels and owns nothing.
                {
                    auto sc = getBusBuffer (buffer, true, 1);
                    scView.setDataToReferTo (sc.getArrayOfWritePointers(),
                                            sc.getNumChannels(), sc.getNumSamples());
                }
                if (scView.getNumChannels() > 0 && scView.getNumSamples() > 0)
                    sidechain = &scView;
            }

        // With a second input bus, `buffer` holds BOTH buses back to back.
        auto mainBus = getBusBuffer (buffer, true, 0);

        // One stateful module instance cannot safely occupy two serial slots.
        // Existing/automated duplicate states remain loadable, but only the first
        // active occurrence processes. This preserves the fixed host parameter
        // contract without processing one delay/filter state twice per block.
        std::array<int, kNumSlots> processedModules {};
        int numProcessed = 0;
        for (int s = 0; s < kNumSlots; ++s)
        {
            const auto& controls = slotControls_[(size_t) s];
            const int sel = (int) controls.selection->load();
            if (sel <= 0) continue;
            if (controls.bypass->load() > 0.5f) continue;

            const int moduleIndex = sel - 1;
            bool duplicate = false;
            for (int i = 0; i < numProcessed; ++i)
                duplicate = duplicate || processedModules[(size_t) i] == moduleIndex;
            if (duplicate) continue;

            if (auto* m = moduleAt (moduleIndex))
            {
                processedModules[(size_t) numProcessed++] = moduleIndex;
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

protected:
    // Register a factory preset from a chain of module ids plus any parameter
    // overrides. Slot indices are resolved from the LIVE roster by moduleId(), not
    // hard-coded — so a preset keeps meaning the same chain even as the roster
    // grows, and a typo'd id fails loudly here at construction instead of silently
    // loading the wrong processor.
    void addChainPreset (const juce::String& name,
                         const juce::StringArray& moduleIds,
                         sattari::presets::SattariPresetManager::PresetValues params = {})
    {
        sattari::presets::SattariPresetManager::PresetValues values;

        // Every slot is written, so a preset always fully defines the chain — a
        // leftover module from whatever was loaded before can never survive.
        for (int s = 0; s < kNumSlots; ++s)
        {
            float slotValue = 0.0f;   // Empty
            if (s < moduleIds.size())
            {
                const int idx = indexOfModule (moduleIds[s]);
                jassert (idx >= 0);   // unknown module id in a factory preset
                if (idx >= 0) slotValue = (float) (idx + 1);
            }
            values.push_back ({ slotParamId (s), slotValue });
            values.push_back ({ slotBypassParamId (s), 0.0f });
        }

        for (auto& kv : params)
            values.push_back (kv);

        presetBank.addFactoryPreset (name, std::move (values));
    }

    int indexOfModule (const juce::String& id) const noexcept
    {
        for (int i = 0; i < (int) modules.size(); ++i)
            if (id == modules[(size_t) i]->moduleId())
                return i;
        return -1;
    }

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
        std::array<int, kNumSlots> activeModules {};
        int numActive = 0;
        for (int s = 0; s < kNumSlots; ++s)
        {
            const auto& controls = slotControls_[(size_t) s];
            const int sel = (int) controls.selection->load();
            const bool byp = controls.bypass->load() > 0.5f;
            if (sel <= 0 || byp) continue;

            const int moduleIndex = sel - 1;
            bool duplicate = false;
            for (int i = 0; i < numActive; ++i)
                duplicate = duplicate || activeModules[(size_t) i] == moduleIndex;
            if (duplicate) continue;

            if (auto* m = const_cast<SattariRackProcessor*> (this)->moduleAt (moduleIndex))
            {
                activeModules[(size_t) numActive++] = moduleIndex;
                total += m->latencySamples();
            }
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
    sattari::presets::SattariPresetManager presetBank;   // declared AFTER apvts

private:
    struct SlotControls
    {
        std::atomic<float>* selection = nullptr;
        std::atomic<float>* bypass = nullptr;
    };

    int currentProgram_ = 0;
    double sr_ = 44100.0;
    std::array<SlotControls, kNumSlots> slotControls_ {};
    std::atomic<int> pendingLatency_ { 0 };
    int reportedLatency_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariRackProcessor)
};

} // namespace sattari::rack
