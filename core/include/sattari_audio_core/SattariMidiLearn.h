#pragma once

// =============================================================================
//  Sattari Audio Suite — minimal MIDI Learn
// -----------------------------------------------------------------------------
//  Single shared learner that processBlock feeds incoming MIDI into. When the
//  user arms a parameter (armForParam("paramId")), the next CC seen binds that
//  CC -> param. After that, incoming CCs steer the param value linearly.
//
//  Storage:
//   * A fixed atomic table maps ccNumber -> parameter index.
//   * Persist via saveTo/loadFrom (a ValueTree subtree). Plugins add this
//     subtree to their getStateInformation alongside the APVTS state.
//
//  Realtime behaviour:
//   * processMidi() walks the buffer once and only touches atomics.
//   * Host parameter gestures/notifications are deferred to the message thread
//     through AsyncUpdater, because host callbacks are not realtime-safe.
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace sattari::midi
{

class SattariMidiLearn : private juce::AsyncUpdater
{
public:
    //  THE CONSTRUCTOR MUST NOT TOUCH `state`. Five plugins declare their
    //  SattariMidiLearn member ABOVE their apvts (Compass, Key, Harmony, Maqam,
    //  Vocal Master), and members initialise in DECLARATION order — so this
    //  constructor runs while apvts is still raw memory. Binding the reference is
    //  legal; dereferencing it is not, and walking state.processor.getParameters()
    //  here segfaulted every one of them on instantiation.
    //
    //  So the parameter table is built lazily, on the first MESSAGE-THREAD use.
    //  That is not a compromise: MIDI learn does nothing until a mapping exists,
    //  and a mapping can only arrive by the user arming one (armForParam) or by
    //  state restore (loadFrom) — both message thread, both after construction.
    explicit SattariMidiLearn (juce::AudioProcessorValueTreeState& state)
        : state_ (state)
    {
        for (auto& mappedIndex : ccToParameterIndex)
            mappedIndex.store (-1, std::memory_order_relaxed);
    }

    // Optional: call once from prepareToPlay if you want the table warm before
    // the first mapping. Never required, and never call it from the audio thread.
    void prepare() { ensureBuilt(); }

    ~SattariMidiLearn() override { cancelPendingUpdate(); }

    // Start listening for the next CC to bind to this param.
    void armForParam (const juce::String& paramId)
    {
        ensureBuilt();
        armedParameterIndex.store (indexForParam (paramId), std::memory_order_release);
    }
    void cancelArm() noexcept { armedParameterIndex.store (-1, std::memory_order_release); }
    bool isArmed() const noexcept { return armedParameterIndex.load (std::memory_order_acquire) >= 0; }
    juce::String armedFor() const
    {
        const int index = armedParameterIndex.load (std::memory_order_acquire);
        return juce::isPositiveAndBelow (index, (int) parameterIds.size())
             ? parameterIds[(size_t) index] : juce::String();
    }

    // Drop an existing mapping.
    void clearMapping (int ccNumber) noexcept
    {
        if (juce::isPositiveAndBelow (ccNumber, (int) ccToParameterIndex.size()))
            ccToParameterIndex[(size_t) ccNumber].store (-1, std::memory_order_release);
    }
    void clearAll() noexcept
    {
        for (auto& mappedIndex : ccToParameterIndex)
            mappedIndex.store (-1, std::memory_order_release);
        cancelArm();
    }

    // For UI: what's mapped to a given param (returns -1 if nothing).
    int ccForParam (const juce::String& paramId) const
    {
        ensureBuilt();
        const int parameterIndex = indexForParam (paramId);
        if (parameterIndex < 0) return -1;
        for (int cc = 0; cc < (int) ccToParameterIndex.size(); ++cc)
            if (ccToParameterIndex[(size_t) cc].load (std::memory_order_acquire) == parameterIndex)
                return cc;
        return -1;
    }
    int numMappings() const noexcept
    {
        int count = 0;
        for (const auto& mappedIndex : ccToParameterIndex)
            if (mappedIndex.load (std::memory_order_acquire) >= 0)
                ++count;
        return count;
    }

    // Audio thread entry point. Walks the buffer and queues parameter changes.
    void processMidi (const juce::MidiBuffer& midi)
    {
        // Acquire pairs with the release in ensureBuilt(): if we observe `built`,
        // every table write that preceded it is visible to us. If we do not, no
        // mapping can exist yet, so there is nothing to apply.
        if (! built.load (std::memory_order_acquire))
            return;

        for (const auto m : midi)
        {
            const auto& msg = m.getMessage();
            if (! msg.isController()) continue;
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();   // 0..127

            // If user has armed a param, the next CC binds to it.
            const int armed = armedParameterIndex.exchange (-1, std::memory_order_acq_rel);
            if (armed >= 0)
            {
                ccToParameterIndex[(size_t) cc].store (armed, std::memory_order_release);
                queueParameterValue (armed, val);       // also apply this CC straight away
                continue;
            }

            applyCC (cc, val);
        }
    }

    // --- State persistence ---------------------------------------------------
    void saveTo (juce::ValueTree& parent) const
    {
        ensureBuilt();
        juce::ValueTree v ("MidiLearn");
        for (int cc = 0; cc < (int) ccToParameterIndex.size(); ++cc)
        {
            const int parameterIndex = ccToParameterIndex[(size_t) cc].load (std::memory_order_acquire);
            if (! juce::isPositiveAndBelow (parameterIndex, (int) parameterIds.size()))
                continue;

            juce::ValueTree m ("Map");
            m.setProperty ("cc", cc, nullptr);
            m.setProperty ("p", parameterIds[(size_t) parameterIndex], nullptr);
            v.appendChild (m, nullptr);
        }
        parent.appendChild (v, nullptr);
    }
    void loadFrom (const juce::ValueTree& parent)
    {
        ensureBuilt();
        clearAll();
        const auto v = parent.getChildWithName ("MidiLearn");
        if (! v.isValid()) return;
        for (int i = 0; i < v.getNumChildren(); ++i)
        {
            const auto m = v.getChild (i);
            const int cc = (int) m.getProperty ("cc", -1);
            const juce::String pid = m.getProperty ("p", "").toString();
            const int parameterIndex = indexForParam (pid);
            if (juce::isPositiveAndBelow (cc, (int) ccToParameterIndex.size())
                && parameterIndex >= 0)
                ccToParameterIndex[(size_t) cc].store (parameterIndex, std::memory_order_release);
        }
    }

private:
    // Message thread only. Built once, never rebuilt, so the audio thread needs
    // no lock — just the acquire/release handshake on `built`.
    void ensureBuilt() const
    {
        if (built.load (std::memory_order_acquire))
            return;

        for (auto* parameter : state_.processor.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            {
                parameterIds.push_back (withId->paramID);
                parameters.push_back (parameter);
            }

        parameterCount = parameters.size();
        pendingValues = std::make_unique<std::atomic<float>[]> (parameterCount);
        pendingDirty  = std::make_unique<std::atomic<bool>[]> (parameterCount);
        for (size_t i = 0; i < parameterCount; ++i)
        {
            pendingValues[i].store (0.0f, std::memory_order_relaxed);
            pendingDirty[i].store (false, std::memory_order_relaxed);
        }

        built.store (true, std::memory_order_release);   // publishes everything above
    }

    int indexForParam (const juce::String& paramId) const
    {
        for (size_t i = 0; i < parameterIds.size(); ++i)
            if (parameterIds[i] == paramId)
                return (int) i;
        return -1;
    }

    void applyCC (int cc, int value) noexcept
    {
        if (! juce::isPositiveAndBelow (cc, (int) ccToParameterIndex.size()))
            return;
        queueParameterValue (
            ccToParameterIndex[(size_t) cc].load (std::memory_order_acquire), value);
    }

    void queueParameterValue (int parameterIndex, int midiValue) noexcept
    {
        if (! juce::isPositiveAndBelow (parameterIndex, (int) parameterCount))
            return;

        pendingValues[(size_t) parameterIndex].store (
            juce::jlimit (0.0f, 1.0f, (float) midiValue / 127.0f),
            std::memory_order_relaxed);
        pendingDirty[(size_t) parameterIndex].store (true, std::memory_order_release);

        if (! asyncPosted.exchange (true, std::memory_order_acq_rel))
            triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        // Clear first: a CC arriving while we drain the table will schedule the
        // next pass, including when it targets an index we already visited.
        asyncPosted.store (false, std::memory_order_release);

        for (size_t i = 0; i < parameterCount; ++i)
        {
            if (! pendingDirty[i].exchange (false, std::memory_order_acq_rel))
                continue;

            auto* parameter = parameters[i];
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (
                pendingValues[i].load (std::memory_order_relaxed));
            parameter->endChangeGesture();
        }
    }

    std::array<std::atomic<int>, 128> ccToParameterIndex;
    std::atomic<int> armedParameterIndex { -1 };
    juce::AudioProcessorValueTreeState& state_;

    // `mutable` because ensureBuilt() is a lazy cache reachable from the const
    // accessors (ccForParam, saveTo). Written once, on the message thread, and
    // published to the audio thread by the release store to `built`.
    mutable std::atomic<bool> built { false };
    mutable std::vector<juce::String> parameterIds;
    mutable std::vector<juce::AudioProcessorParameter*> parameters;
    mutable size_t parameterCount = 0;
    mutable std::unique_ptr<std::atomic<float>[]> pendingValues;
    mutable std::unique_ptr<std::atomic<bool>[]> pendingDirty;
    std::atomic<bool> asyncPosted { false };
};

// Build + show the standard "MIDI Learn..." popup. Lists every continuous/
// switch parameter in the APVTS, annotates with the current CC mapping if any,
// and arms learning on selection. Plus a "Clear all mappings" entry. Editors
// drop one of these in their button onClick lambda.
inline void showLearnPopup (SattariMidiLearn& learner,
                            juce::Component& anchor,
                            juce::AudioProcessorValueTreeState& apvts)
{
    auto menu = std::make_shared<juce::PopupMenu>();

    // Collect (id, label) for every named param. We iterate the parameter
    // graph rather than APVTS internals because that exposes both APVTS-owned
    // params and any external parameter groups uniformly.
    auto ids = std::make_shared<juce::StringArray>();
    menu->addSectionHeader ("Arm parameter for next CC");

    auto* proc = &apvts.processor;
    for (auto* p : proc->getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        {
            const auto& id = withId->paramID;
            const auto label = withId->name + (juce::String) "";
            const int cc = learner.ccForParam (id);
            const juce::String suffix = cc >= 0 ? " (CC " + juce::String (cc) + ")" : juce::String();
            ids->add (id);
            menu->addItem ((int) ids->size(), label + suffix);
        }
    }
    menu->addSeparator();
    menu->addItem (10000, "Clear all mappings");

    menu->showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
        [ids, &learner] (int choice)
        {
            if (choice <= 0) return;
            if (choice == 10000) { learner.clearAll(); return; }
            const int idx = choice - 1;
            if (juce::isPositiveAndBelow (idx, ids->size()))
                learner.armForParam ((*ids)[idx]);
        });
}

} // namespace sattari::midi
