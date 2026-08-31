#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include "SoftTakeover.h"

/**
 * Hardware-controller MIDI mapping with learn mode.
 *
 * Lock-free and RT-safe: the controller→target maps are fixed-size
 * std::array<std::atomic<int>, 128>, so the audio thread reads them while the
 * message thread (learn / clear / state restore) writes them with no locks and
 * no allocation.
 *
 * Two kinds of target:
 *   • Continuous (CC)  → publishes a normalized APVTS change for the processor's
 *                        message-thread dispatcher (never host-notifies on audio).
 *   • Trigger (Note)   → a discrete deck action (Play toggle / Cue), collected
 *                        into a fixed buffer for the processor to apply.
 *
 * Learn flow: the UI calls beginLearnCc/Note(targetIdx); the next matching MIDI
 * message on the audio thread binds that controller to the target and publishes
 * a one-shot result the UI polls via pollLearnResult().
 */
class MidiMappingManager
{
public:
    enum NoteAction
    {
        ActionPlay = 0, ActionCue, ActionPad, ActionSync, ActionLoop,
        ActionHotCue, ActionStemMute, ActionSafeMode
    };

    struct CcTarget
    {
        CcTarget (juce::String targetName, juce::RangedAudioParameter* parameter)
            : name (std::move (targetName)), param (parameter) {}
        juce::String name;
        juce::RangedAudioParameter* param = nullptr;
        SoftTakeover softTakeover;
    };
    struct NoteTarget
    {
        NoteTarget (juce::String targetName, int deckIndex, int noteAction,
                    int actionDetail = 0, std::atomic<float>* feedbackValue = nullptr)
            : name (std::move (targetName)), deck (deckIndex), action (noteAction),
              detail (actionDetail), feedback (feedbackValue) {}
        juce::String name;
        int deck = 0;
        int action = ActionPlay;
        int detail = 0;
        std::atomic<float>* feedback = nullptr;
    };
    struct Trigger    { int deck = 0; int action = 0; int detail = 0; };

    static constexpr int maxCcTargets = 256;

    MidiMappingManager()
    {
        for (auto& a : ccMap)      a.store (-1, std::memory_order_relaxed);
        for (auto& a : noteMap)    a.store (-1, std::memory_order_relaxed);
        for (auto& a : ccChannel)  a.store (0, std::memory_order_relaxed);
        for (auto& a : noteChannel) a.store (0, std::memory_order_relaxed);
        for (auto& a : lastSentCc) a.store (-1, std::memory_order_relaxed);
        for (auto& a : lastSentNote) a.store (-1, std::memory_order_relaxed);
        for (auto& a : pendingCcValue) a.store (0.0f, std::memory_order_relaxed);
        for (auto& a : pendingCcGeneration) a.store (0, std::memory_order_relaxed);
        for (auto& a : pendingCcDirty) a.store (false, std::memory_order_relaxed);
    }

    // Call once before audio starts (message thread).
    void setTargets (std::vector<CcTarget> cc, std::vector<NoteTarget> note)
    {
        if ((int) cc.size() > maxCcTargets)
            cc.erase (cc.begin() + maxCcTargets, cc.end());
        ccTargets   = std::move (cc);
        noteTargets = std::move (note);
        for (int target = 0; target < (int) ccTargets.size(); ++target)
            if (ccTargets[(size_t) target].param != nullptr)
                pendingCcValue[(size_t) target].store (
                    ccTargets[(size_t) target].param->getValue(), std::memory_order_relaxed);
    }

    // ── audio thread ──────────────────────────────────────────────────────────
    // Applies mapped CCs to parameters; returns the number of note triggers,
    // readable via triggered(i).
    int processBlock (const juce::MidiBuffer& midi)
    {
        int triggers = 0;

        for (const auto metadata : midi)
        {
            const auto m = metadata.getMessage();

            if (m.isController())
            {
                const int cc  = m.getControllerNumber();
                const int val = m.getControllerValue();
                if (cc < 0 || cc > 127) continue;

                if (learnType.load (std::memory_order_relaxed) == 0)
                {
                    const int tgt = learnTarget.load (std::memory_order_relaxed);
                    if (tgt >= 0)
                    {
                        bindCc (cc, m.getChannel(), tgt);
                        publishLearnResult (cc, m.getChannel(), 0, tgt);
                        clearLearnState();
                    }
                }
                else
                {
                    const int tgt = ccMap[(size_t) cc].load (std::memory_order_relaxed);
                    const int channel = ccChannel[(size_t) cc].load (std::memory_order_relaxed);
                    if ((channel == 0 || channel == m.getChannel())
                        && tgt >= 0 && tgt < (int) ccTargets.size()
                        && ccTargets[(size_t) tgt].param != nullptr)
                    {
                        auto& ct = ccTargets[(size_t) tgt];
                        const float hwNorm = (float) val / 127.0f;
                        const float current = pendingCcDirty[(size_t) tgt].load (std::memory_order_acquire)
                            ? pendingCcValue[(size_t) tgt].load (std::memory_order_relaxed)
                            : ct.param->getValue();
                        if (ct.softTakeover.update (hwNorm, current))
                        {
                            pendingCcValue[(size_t) tgt].store (hwNorm, std::memory_order_relaxed);
                            pendingCcGeneration[(size_t) tgt].fetch_add (1, std::memory_order_release);
                            pendingCcDirty[(size_t) tgt].store (true, std::memory_order_release);
                        }
                    }
                }
            }
            else if (m.isNoteOn())
            {
                const int note = m.getNoteNumber();
                if (note < 0 || note > 127) continue;

                if (learnType.load (std::memory_order_relaxed) == 1)
                {
                    const int tgt = learnTarget.load (std::memory_order_relaxed);
                    if (tgt >= 0)
                    {
                        bindNote (note, m.getChannel(), tgt);
                        publishLearnResult (note, m.getChannel(), 1, tgt);
                        clearLearnState();
                    }
                }
                else
                {
                    const int tgt = noteMap[(size_t) note].load (std::memory_order_relaxed);
                    const int channel = noteChannel[(size_t) note].load (std::memory_order_relaxed);
                    if ((channel == 0 || channel == m.getChannel())
                        && tgt >= 0 && tgt < (int) noteTargets.size()
                        && triggers < (int) noteTriggers.size())
                        noteTriggers[(size_t) triggers++] = { noteTargets[(size_t) tgt].deck,
                                                              noteTargets[(size_t) tgt].action,
                                                              noteTargets[(size_t) tgt].detail };
                }
            }
        }

        return triggers;
    }

    Trigger triggered (int i) const noexcept { return noteTriggers[(size_t) i]; }

    // The audio thread only publishes normalized controller moves. The processor's
    // timer drains them and performs host notification/event capture on the message
    // thread, keeping MIDI hardware out of the real-time lock path.
    template <typename Callback>
    int servicePendingCc (Callback&& apply)
    {
        int serviced = 0;
        for (int target = 0; target < (int) ccTargets.size(); ++target)
        {
            if (! pendingCcDirty[(size_t) target].load (std::memory_order_acquire))
                continue;
            const auto generation = pendingCcGeneration[(size_t) target].load (std::memory_order_acquire);
            const float value = pendingCcValue[(size_t) target].load (std::memory_order_relaxed);
            apply (target, value);
            if (pendingCcGeneration[(size_t) target].load (std::memory_order_acquire) == generation)
                pendingCcDirty[(size_t) target].store (false, std::memory_order_release);
            ++serviced;
        }
        return serviced;
    }

    // ── audio thread: motorized-fader MIDI feedback ─────────────────────────────
    // Emits a CC for every mapped fader whose value changed since the last call,
    // so motorized/LED controllers track the UI. Change-detection also prevents
    // MIDI loops: an incoming CC leaves the param at that value, so the feedback
    // value matches and is suppressed. Call resyncFeedback() to force a full resend
    // (e.g. after a preset/deck switch).
    void writeFeedback (juce::MidiBuffer& out, int sampleOffset = 0)
    {
        const bool force = forceFeedback.exchange (false, std::memory_order_relaxed);
        for (int cc = 0; cc < 128; ++cc)
        {
            const int tgt = ccMap[(size_t) cc].load (std::memory_order_relaxed);
            if (tgt < 0 || tgt >= (int) ccTargets.size() || ccTargets[(size_t) tgt].param == nullptr) continue;
            const float normalized = pendingCcDirty[(size_t) tgt].load (std::memory_order_acquire)
                ? pendingCcValue[(size_t) tgt].load (std::memory_order_relaxed)
                : ccTargets[(size_t) tgt].param->getValue();
            const int val = juce::jlimit (0, 127, (int) std::round (normalized * 127.0f));
            if (force || val != lastSentCc[(size_t) cc].load (std::memory_order_relaxed))
            {
                lastSentCc[(size_t) cc].store (val, std::memory_order_relaxed);
                const int channel = ccChannel[(size_t) cc].load (std::memory_order_relaxed);
                out.addEvent (juce::MidiMessage::controllerEvent (
                    channel == 0 ? 1 : juce::jlimit (1, 16, channel), cc, val), sampleOffset);
            }
        }
        // Play and Sync are host-visible toggles, so mapped illuminated buttons
        // follow UI/automation changes just like motorized CC controls. Actions
        // without an atomic state source remain input-only rather than guessing.
        for (int note = 0; note < 128; ++note)
        {
            const int target = noteMap[(size_t) note].load (std::memory_order_relaxed);
            if (target < 0 || target >= (int) noteTargets.size()) continue;
            const auto* value = noteTargets[(size_t) target].feedback;
            if (value == nullptr) continue;
            const int on = value->load (std::memory_order_relaxed) > 0.5f ? 1 : 0;
            if (force || on != lastSentNote[(size_t) note].load (std::memory_order_relaxed))
            {
                lastSentNote[(size_t) note].store (on, std::memory_order_relaxed);
                const int channel = noteChannel[(size_t) note].load (std::memory_order_relaxed);
                const int useChannel = channel == 0 ? 1 : juce::jlimit (1, 16, channel);
                out.addEvent (on ? juce::MidiMessage::noteOn (useChannel, note, (juce::uint8) 127)
                                 : juce::MidiMessage::noteOff (useChannel, note), sampleOffset);
            }
        }
    }
    void resyncFeedback() noexcept { forceFeedback.store (true, std::memory_order_relaxed); }

    // ── message thread queries ─────────────────────────────────────────────────
    int numCc()   const noexcept { return (int) ccTargets.size(); }
    int numNote() const noexcept { return (int) noteTargets.size(); }
    bool hasBindings() const noexcept
    {
        for (const auto& v : ccMap)
            if (v.load (std::memory_order_relaxed) >= 0) return true;
        for (const auto& v : noteMap)
            if (v.load (std::memory_order_relaxed) >= 0) return true;
        return false;
    }
    juce::String ccName   (int i) const { return juce::isPositiveAndBelow (i, (int) ccTargets.size())   ? ccTargets[(size_t) i].name   : juce::String(); }
    juce::String noteName (int i) const { return juce::isPositiveAndBelow (i, (int) noteTargets.size()) ? noteTargets[(size_t) i].name : juce::String(); }
    int ccTargetForName (const juce::String& name) const
    {
        for (int i = 0; i < (int) ccTargets.size(); ++i)
            if (ccTargets[(size_t) i].name == name) return i;
        return -1;
    }
    int noteTargetForName (const juce::String& name) const
    {
        for (int i = 0; i < (int) noteTargets.size(); ++i)
            if (noteTargets[(size_t) i].name == name) return i;
        return -1;
    }
    juce::RangedAudioParameter* ccParameter (int i) const noexcept
    { return juce::isPositiveAndBelow (i, (int) ccTargets.size()) ? ccTargets[(size_t) i].param : nullptr; }

    int ccForTarget (int targetIdx) const
    {
        for (int cc = 0; cc < 128; ++cc)
            if (ccMap[(size_t) cc].load (std::memory_order_relaxed) == targetIdx) return cc;
        return -1;
    }
    int noteForTarget (int targetIdx) const
    {
        for (int n = 0; n < 128; ++n)
            if (noteMap[(size_t) n].load (std::memory_order_relaxed) == targetIdx) return n;
        return -1;
    }
    int ccChannelForTarget (int targetIdx) const
    {
        const int cc = ccForTarget (targetIdx);
        return cc >= 0 ? ccChannel[(size_t) cc].load (std::memory_order_relaxed) : 0;
    }
    int noteChannelForTarget (int targetIdx) const
    {
        const int note = noteForTarget (targetIdx);
        return note >= 0 ? noteChannel[(size_t) note].load (std::memory_order_relaxed) : 0;
    }

    // ── learn control ───────────────────────────────────────────────────────────
    void beginLearnCc   (int targetIdx) { learnTarget.store (targetIdx, std::memory_order_relaxed); learnType.store (0, std::memory_order_relaxed); }
    void beginLearnNote (int targetIdx) { learnTarget.store (targetIdx, std::memory_order_relaxed); learnType.store (1, std::memory_order_relaxed); }
    void cancelLearn()   { clearLearnState(); }
    bool isLearning() const noexcept { return learnType.load (std::memory_order_relaxed) >= 0; }
    int  learningType()   const noexcept { return learnType.load (std::memory_order_relaxed); }
    int  learningTarget() const noexcept { return learnTarget.load (std::memory_order_relaxed); }

    struct LearnResult { bool valid = false; bool isCc = false; int controller = -1; int midiChannel = 0; int targetIdx = -1; };
    LearnResult pollLearnResult()
    {
        LearnResult r;
        if (resultReady.exchange (false, std::memory_order_acquire))
        {
            r.valid      = true;
            r.isCc       = resultType.load (std::memory_order_relaxed) == 0;
            r.controller = resultController.load (std::memory_order_relaxed);
            r.midiChannel = resultChannel.load (std::memory_order_relaxed);
            r.targetIdx  = resultTarget.load (std::memory_order_relaxed);
        }
        return r;
    }

    void clearAll()
    {
        for (auto& a : ccMap)   a.store (-1, std::memory_order_relaxed);
        for (auto& a : noteMap) a.store (-1, std::memory_order_relaxed);
        for (auto& a : ccChannel) a.store (0, std::memory_order_relaxed);
        for (auto& a : noteChannel) a.store (0, std::memory_order_relaxed);
        for (auto& a : pendingCcDirty) a.store (false, std::memory_order_relaxed);
        for (auto& a : lastSentCc) a.store (-1, std::memory_order_relaxed);
        for (auto& a : lastSentNote) a.store (-1, std::memory_order_relaxed);
        forceFeedback.store (true, std::memory_order_relaxed);
    }

    void clearTarget (bool isCc, int targetIdx)
    {
        auto& map = isCc ? ccMap : noteMap;
        auto& channels = isCc ? ccChannel : noteChannel;
        for (size_t i = 0; i < map.size(); ++i)
            if (map[i].load (std::memory_order_relaxed) == targetIdx)
            {
                map[i].store (-1, std::memory_order_relaxed);
                channels[i].store (0, std::memory_order_relaxed);
            }
    }

    // ── persistence (compact "C7=0;C8=2;N36=0" form) ───────────────────────────
    juce::String serialize() const
    {
        juce::StringArray parts;
        for (int cc = 0; cc < 128; ++cc)
        {
            const int t = ccMap[(size_t) cc].load (std::memory_order_relaxed);
            if (t >= 0) parts.add ("C" + juce::String (cc) + "@"
                                   + juce::String (ccChannel[(size_t) cc].load (std::memory_order_relaxed))
                                   + "=" + juce::String (t));
        }
        for (int n = 0; n < 128; ++n)
        {
            const int t = noteMap[(size_t) n].load (std::memory_order_relaxed);
            if (t >= 0) parts.add ("N" + juce::String (n) + "@"
                                   + juce::String (noteChannel[(size_t) n].load (std::memory_order_relaxed))
                                   + "=" + juce::String (t));
        }
        return parts.joinIntoString (";");
    }

    void deserialize (const juce::String& s)
    {
        clearAll();
        for (const auto& tok : juce::StringArray::fromTokens (s, ";", {}))
        {
            const auto t = tok.trim();
            if (t.length() < 4) continue;
            const auto kind = t[0];
            const int eq = t.indexOfChar ('=');
            if (eq < 2) continue;
            const auto address = t.substring (1, eq);
            const int at = address.indexOfChar ('@');
            const int controller = address.substring (0, at < 0 ? address.length() : at).getIntValue();
            const int channel = at < 0 ? 0 : juce::jlimit (0, 16, address.substring (at + 1).getIntValue());
            const int target     = t.substring (eq + 1).getIntValue();
            if (controller < 0 || controller > 127 || target < 0) continue;
            if (kind == 'C' && target < (int) ccTargets.size())
                bindCc (controller, channel, target);
            else if (kind == 'N' && target < (int) noteTargets.size())
                bindNote (controller, channel, target);
        }
    }

    // Portable controller presets bind by stable target name rather than vector
    // index. A preset therefore survives target reordering between app versions.
    juce::String serializeNamedPreset (const juce::String& name) const
    {
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty ("format", "StemDeck MIDI Map");
        root->setProperty ("version", 2);
        root->setProperty ("name", name);
        juce::Array<juce::var> bindings;
        for (int cc = 0; cc < 128; ++cc)
        {
            const int target = ccMap[(size_t) cc].load (std::memory_order_relaxed);
            if (! juce::isPositiveAndBelow (target, (int) ccTargets.size())) continue;
            auto binding = std::make_unique<juce::DynamicObject>();
            binding->setProperty ("type", "cc");
            binding->setProperty ("controller", cc);
            binding->setProperty ("channel", ccChannel[(size_t) cc].load (std::memory_order_relaxed));
            binding->setProperty ("target", ccTargets[(size_t) target].name);
            bindings.add (juce::var (binding.release()));
        }
        for (int note = 0; note < 128; ++note)
        {
            const int target = noteMap[(size_t) note].load (std::memory_order_relaxed);
            if (! juce::isPositiveAndBelow (target, (int) noteTargets.size())) continue;
            auto binding = std::make_unique<juce::DynamicObject>();
            binding->setProperty ("type", "note");
            binding->setProperty ("controller", note);
            binding->setProperty ("channel", noteChannel[(size_t) note].load (std::memory_order_relaxed));
            binding->setProperty ("target", noteTargets[(size_t) target].name);
            bindings.add (juce::var (binding.release()));
        }
        root->setProperty ("bindings", bindings);
        return juce::JSON::toString (juce::var (root.release()), true);
    }

    bool deserializeNamedPreset (const juce::String& json, juce::String& error)
    {
        const auto parsed = juce::JSON::parse (json);
        const auto* object = parsed.getDynamicObject();
        if (object == nullptr || object->getProperty ("format").toString()
                                    != "StemDeck MIDI Map")
        {
            error = "Not a StemDeck MIDI map";
            return false;
        }
        const auto* bindings = object->getProperty ("bindings").getArray();
        if (bindings == nullptr)
        {
            error = "The MIDI map has no bindings";
            return false;
        }

        clearAll();
        int restored = 0;
        for (const auto& value : *bindings)
        {
            const auto* binding = value.getDynamicObject();
            if (binding == nullptr) continue;
            const int controller = (int) binding->getProperty ("controller");
            const int channel = juce::jlimit (0, 16, (int) binding->getProperty ("channel"));
            const auto targetName = binding->getProperty ("target").toString();
            if (! juce::isPositiveAndBelow (controller, 128) && controller != 0)
                continue;
            if (binding->getProperty ("type").toString() == "cc")
            {
                for (int target = 0; target < (int) ccTargets.size(); ++target)
                    if (ccTargets[(size_t) target].name == targetName)
                    {
                        bindCc (controller, channel, target);
                        ++restored;
                        break;
                    }
            }
            else if (binding->getProperty ("type").toString() == "note")
            {
                for (int target = 0; target < (int) noteTargets.size(); ++target)
                    if (noteTargets[(size_t) target].name == targetName)
                    {
                        bindNote (controller, channel, target);
                        ++restored;
                        break;
                    }
            }
        }
        forceFeedback.store (true, std::memory_order_relaxed);
        if (restored == 0 && ! bindings->isEmpty())
        {
            error = "No matching StemDeck targets were found";
            return false;
        }
        return true;
    }

private:
    void bindCc (int controller, int channel, int target)
    {
        if (! juce::isPositiveAndBelow (controller, 128) && controller != 0) return;
        for (int cc = 0; cc < 128; ++cc)
            if (ccMap[(size_t) cc].load (std::memory_order_relaxed) == target)
            {
                ccMap[(size_t) cc].store (-1, std::memory_order_relaxed);
                ccChannel[(size_t) cc].store (0, std::memory_order_relaxed);
            }
        ccMap[(size_t) controller].store (target, std::memory_order_relaxed);
        ccChannel[(size_t) controller].store (juce::jlimit (0, 16, channel), std::memory_order_relaxed);
        if (juce::isPositiveAndBelow (target, (int) ccTargets.size()))
            ccTargets[(size_t) target].softTakeover.reset();
        lastSentCc[(size_t) controller].store (-1, std::memory_order_relaxed);
    }

    void bindNote (int controller, int channel, int target)
    {
        if (! juce::isPositiveAndBelow (controller, 128) && controller != 0) return;
        for (int note = 0; note < 128; ++note)
            if (noteMap[(size_t) note].load (std::memory_order_relaxed) == target)
            {
                noteMap[(size_t) note].store (-1, std::memory_order_relaxed);
                noteChannel[(size_t) note].store (0, std::memory_order_relaxed);
            }
        noteMap[(size_t) controller].store (target, std::memory_order_relaxed);
        noteChannel[(size_t) controller].store (juce::jlimit (0, 16, channel), std::memory_order_relaxed);
    }

    void clearLearnState()
    {
        learnType.store (-1, std::memory_order_relaxed);
        learnTarget.store (-1, std::memory_order_relaxed);
    }

    void publishLearnResult (int controller, int channel, int type, int target)
    {
        resultController.store (controller, std::memory_order_relaxed);
        resultChannel.store (channel, std::memory_order_relaxed);
        resultType.store (type, std::memory_order_relaxed);
        resultTarget.store (target, std::memory_order_relaxed);
        resultReady.store (true, std::memory_order_release);
    }

    std::vector<CcTarget>   ccTargets;
    std::vector<NoteTarget> noteTargets;

    std::array<std::atomic<int>, 128> ccMap;
    std::array<std::atomic<int>, 128> noteMap;
    std::array<std::atomic<int>, 128> ccChannel;
    std::array<std::atomic<int>, 128> noteChannel;
    std::array<std::atomic<int>, 128> lastSentCc {};   // motorized-fader feedback (-1 = unsent)
    std::array<std::atomic<int>, 128> lastSentNote {}; // illuminated-button feedback
    std::atomic<bool> forceFeedback { true };          // resend all CCs on next writeFeedback
    std::array<std::atomic<float>, maxCcTargets> pendingCcValue;
    std::array<std::atomic<std::uint32_t>, maxCcTargets> pendingCcGeneration;
    std::array<std::atomic<bool>, maxCcTargets> pendingCcDirty;

    std::atomic<int> learnType   { -1 };   // -1 none, 0 cc, 1 note
    std::atomic<int> learnTarget { -1 };

    std::atomic<int>  resultController { -1 };
    std::atomic<int>  resultChannel    { 0 };
    std::atomic<int>  resultType       { -1 };
    std::atomic<int>  resultTarget     { -1 };
    std::atomic<bool> resultReady      { false };

    std::array<Trigger, 64> noteTriggers {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMappingManager)
};
