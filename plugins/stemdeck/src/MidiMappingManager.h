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
 *   • Continuous (CC)  → an APVTS parameter, driven 0..1 from CC value 0..127.
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
    enum NoteAction { ActionPlay = 0, ActionCue = 1, ActionPad = 2 };

    struct CcTarget   { juce::String name; juce::RangedAudioParameter* param = nullptr; SoftTakeover softTakeover; };
    struct NoteTarget { juce::String name; int deck = 0; int action = ActionPlay; };
    struct Trigger    { int deck = 0; int action = 0; };

    MidiMappingManager()
    {
        for (auto& a : ccMap)      a.store (-1, std::memory_order_relaxed);
        for (auto& a : noteMap)    a.store (-1, std::memory_order_relaxed);
        for (auto& a : lastSentCc) a.store (-1, std::memory_order_relaxed);
    }

    // Call once before audio starts (message thread).
    void setTargets (std::vector<CcTarget> cc, std::vector<NoteTarget> note)
    {
        ccTargets   = std::move (cc);
        noteTargets = std::move (note);
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
                        ccMap[(size_t) cc].store (tgt, std::memory_order_relaxed);
                        publishLearnResult (cc, 0, tgt);
                        clearLearnState();
                    }
                }
                else
                {
                    const int tgt = ccMap[(size_t) cc].load (std::memory_order_relaxed);
                    if (tgt >= 0 && tgt < (int) ccTargets.size() && ccTargets[(size_t) tgt].param != nullptr)
                    {
                        auto& ct = ccTargets[(size_t) tgt];
                        const float hwNorm = (float) val / 127.0f;
                        if (ct.softTakeover.update (hwNorm, ct.param->getValue()))
                            ct.param->setValueNotifyingHost (hwNorm);
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
                        noteMap[(size_t) note].store (tgt, std::memory_order_relaxed);
                        publishLearnResult (note, 1, tgt);
                        clearLearnState();
                    }
                }
                else
                {
                    const int tgt = noteMap[(size_t) note].load (std::memory_order_relaxed);
                    if (tgt >= 0 && tgt < (int) noteTargets.size() && triggers < (int) noteTriggers.size())
                        noteTriggers[(size_t) triggers++] = { noteTargets[(size_t) tgt].deck,
                                                              noteTargets[(size_t) tgt].action };
                }
            }
        }

        return triggers;
    }

    Trigger triggered (int i) const noexcept { return noteTriggers[(size_t) i]; }

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
            const int val = juce::jlimit (0, 127, (int) std::round (ccTargets[(size_t) tgt].param->getValue() * 127.0f));
            if (force || val != lastSentCc[(size_t) cc].load (std::memory_order_relaxed))
            {
                lastSentCc[(size_t) cc].store (val, std::memory_order_relaxed);
                out.addEvent (juce::MidiMessage::controllerEvent (1, cc, val), sampleOffset);
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

    // ── learn control ───────────────────────────────────────────────────────────
    void beginLearnCc   (int targetIdx) { learnTarget.store (targetIdx, std::memory_order_relaxed); learnType.store (0, std::memory_order_relaxed); }
    void beginLearnNote (int targetIdx) { learnTarget.store (targetIdx, std::memory_order_relaxed); learnType.store (1, std::memory_order_relaxed); }
    void cancelLearn()   { clearLearnState(); }
    bool isLearning() const noexcept { return learnType.load (std::memory_order_relaxed) >= 0; }
    int  learningType()   const noexcept { return learnType.load (std::memory_order_relaxed); }
    int  learningTarget() const noexcept { return learnTarget.load (std::memory_order_relaxed); }

    struct LearnResult { bool valid = false; bool isCc = false; int controller = -1; int targetIdx = -1; };
    LearnResult pollLearnResult()
    {
        LearnResult r;
        if (resultReady.exchange (false, std::memory_order_acquire))
        {
            r.valid      = true;
            r.isCc       = resultType.load (std::memory_order_relaxed) == 0;
            r.controller = resultController.load (std::memory_order_relaxed);
            r.targetIdx  = resultTarget.load (std::memory_order_relaxed);
        }
        return r;
    }

    void clearAll()
    {
        for (auto& a : ccMap)   a.store (-1, std::memory_order_relaxed);
        for (auto& a : noteMap) a.store (-1, std::memory_order_relaxed);
    }

    void clearTarget (bool isCc, int targetIdx)
    {
        auto& map = isCc ? ccMap : noteMap;
        for (auto& a : map)
            if (a.load (std::memory_order_relaxed) == targetIdx)
                a.store (-1, std::memory_order_relaxed);
    }

    // ── persistence (compact "C7=0;C8=2;N36=0" form) ───────────────────────────
    juce::String serialize() const
    {
        juce::StringArray parts;
        for (int cc = 0; cc < 128; ++cc)
        {
            const int t = ccMap[(size_t) cc].load (std::memory_order_relaxed);
            if (t >= 0) parts.add ("C" + juce::String (cc) + "=" + juce::String (t));
        }
        for (int n = 0; n < 128; ++n)
        {
            const int t = noteMap[(size_t) n].load (std::memory_order_relaxed);
            if (t >= 0) parts.add ("N" + juce::String (n) + "=" + juce::String (t));
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
            const int controller = t.substring (1, eq).getIntValue();
            const int target     = t.substring (eq + 1).getIntValue();
            if (controller < 0 || controller > 127 || target < 0) continue;
            if (kind == 'C')      ccMap[(size_t) controller].store (target, std::memory_order_relaxed);
            else if (kind == 'N') noteMap[(size_t) controller].store (target, std::memory_order_relaxed);
        }
    }

private:
    void clearLearnState()
    {
        learnType.store (-1, std::memory_order_relaxed);
        learnTarget.store (-1, std::memory_order_relaxed);
    }

    void publishLearnResult (int controller, int type, int target)
    {
        resultController.store (controller, std::memory_order_relaxed);
        resultType.store (type, std::memory_order_relaxed);
        resultTarget.store (target, std::memory_order_relaxed);
        resultReady.store (true, std::memory_order_release);
    }

    std::vector<CcTarget>   ccTargets;
    std::vector<NoteTarget> noteTargets;

    std::array<std::atomic<int>, 128> ccMap;
    std::array<std::atomic<int>, 128> noteMap;
    std::array<std::atomic<int>, 128> lastSentCc {};   // motorized-fader feedback (-1 = unsent)
    std::atomic<bool> forceFeedback { true };          // resend all CCs on next writeFeedback

    std::atomic<int> learnType   { -1 };   // -1 none, 0 cc, 1 note
    std::atomic<int> learnTarget { -1 };

    std::atomic<int>  resultController { -1 };
    std::atomic<int>  resultType       { -1 };
    std::atomic<int>  resultTarget     { -1 };
    std::atomic<bool> resultReady      { false };

    std::array<Trigger, 64> noteTriggers {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiMappingManager)
};
