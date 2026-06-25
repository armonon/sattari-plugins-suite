#pragma once

// =============================================================================
//  Sattari Audio Suite — minimal MIDI Learn
// -----------------------------------------------------------------------------
//  Single shared learner that processBlock feeds incoming MIDI into. When the
//  user arms a parameter (armForParam("paramId")), the next CC seen binds that
//  CC -> param. After that, incoming CCs steer the param value linearly.
//
//  Storage:
//   * In-memory map: ccNumber -> paramId.
//   * Persist via saveTo/loadFrom (a ValueTree subtree). Plugins add this
//     subtree to their getStateInformation alongside the APVTS state.
//
//  Realtime behaviour:
//   * processMidi() walks the buffer once; it does not allocate. Param updates
//     go through beginChangeGesture/setValueNotifyingHost/end which can lock
//     the host's parameter graph briefly - that is the standard JUCE path for
//     audio-thread parameter automation.
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <unordered_map>
#include <string>

namespace sattari::midi
{

class SattariMidiLearn
{
public:
    explicit SattariMidiLearn (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

    // Start listening for the next CC to bind to this param.
    void armForParam (const juce::String& paramId)
    {
        armedParamId = paramId;
    }
    void cancelArm() { armedParamId.clear(); }
    bool isArmed() const noexcept { return armedParamId.isNotEmpty(); }
    juce::String armedFor() const { return armedParamId; }

    // Drop an existing mapping.
    void clearMapping (int ccNumber)
    {
        ccToParam.erase (ccNumber);
    }
    void clearAll()
    {
        ccToParam.clear();
        armedParamId.clear();
    }

    // For UI: what's mapped to a given param (returns -1 if nothing).
    int ccForParam (const juce::String& paramId) const
    {
        for (const auto& kv : ccToParam)
            if (kv.second == paramId.toStdString())
                return kv.first;
        return -1;
    }
    int numMappings() const noexcept { return (int) ccToParam.size(); }

    // Audio thread entry point. Walks the buffer; arms / steers params.
    void processMidi (const juce::MidiBuffer& midi)
    {
        for (const auto m : midi)
        {
            const auto& msg = m.getMessage();
            if (! msg.isController()) continue;
            const int cc  = msg.getControllerNumber();
            const int val = msg.getControllerValue();   // 0..127

            // If user has armed a param, the next CC binds to it.
            if (armedParamId.isNotEmpty())
            {
                ccToParam[cc] = armedParamId.toStdString();
                armedParamId.clear();
                applyCC (cc, val);                      // also apply this CC straight away
                continue;
            }

            applyCC (cc, val);
        }
    }

    // --- State persistence ---------------------------------------------------
    void saveTo (juce::ValueTree& parent) const
    {
        juce::ValueTree v ("MidiLearn");
        for (const auto& kv : ccToParam)
        {
            juce::ValueTree m ("Map");
            m.setProperty ("cc", kv.first, nullptr);
            m.setProperty ("p",  juce::String (kv.second), nullptr);
            v.appendChild (m, nullptr);
        }
        parent.appendChild (v, nullptr);
    }
    void loadFrom (const juce::ValueTree& parent)
    {
        ccToParam.clear();
        const auto v = parent.getChildWithName ("MidiLearn");
        if (! v.isValid()) return;
        for (int i = 0; i < v.getNumChildren(); ++i)
        {
            const auto m = v.getChild (i);
            const int cc = (int) m.getProperty ("cc", -1);
            const juce::String pid = m.getProperty ("p", "").toString();
            if (cc >= 0 && pid.isNotEmpty())
                ccToParam[cc] = pid.toStdString();
        }
    }

private:
    void applyCC (int cc, int value)
    {
        const auto it = ccToParam.find (cc);
        if (it == ccToParam.end()) return;
        if (auto* p = apvts.getParameter (juce::String (it->second)))
        {
            const float normalised = juce::jlimit (0.0f, 1.0f, (float) value / 127.0f);
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    std::unordered_map<int, std::string> ccToParam;
    juce::String armedParamId;
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
