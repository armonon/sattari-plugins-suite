#pragma once
#include "PluginEditor.h"

namespace harmony_upgrade_test
{
inline int run()
{
    int failures = 0;
    auto check = [&] (bool ok, const char* label)
    {
        std::cout << "  harmonyUpgrade " << label << (ok ? " PASS\n" : " FAIL\n");
        if (! ok) ++failures;
    };
    SattariAutoPitchAudioProcessor p;
    auto state = p.parameters().copyState();
    for (int v = 1; v <= 4; ++v)
    {
        const auto id = "h" + juce::String (v) + "Formant";
        state.removeChild (state.getChildWithProperty ("id", id), nullptr);
        setParameter (p.parameters(), id, 7);
    }
    juce::ValueTree edits ("PitchEdits"), note ("Note");
    note.setProperty ("id", 101, nullptr);
    note.setProperty ("start", 0.0, nullptr);
    note.setProperty ("end", 1.0, nullptr);
    note.setProperty ("source", 57, nullptr);
    note.setProperty ("target", 57, nullptr);
    edits.appendChild (note, nullptr); state.appendChild (edits, nullptr);
    juce::MemoryBlock initial;
    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), initial);
    p.setStateInformation (initial.getData(), (int) initial.getSize());
    for (int v = 1; v <= 4; ++v)
        check (std::abs (p.parameters().getRawParameterValue ("h" + juce::String (v) + "Formant")->load()) < 0.001f,
            "legacy state clears previously edited formant parameter");
    check (p.getPitchEdits().size() == 1 && p.getPitchEdits()[0].harmonyTargets[0] == -1,
        "old note states default to automatic harmony");
    check (p.setHarmonyEditTarget (101, 0, 64) && p.getPitchEdits()[0].targetMidi == 57,
        "independent H1 pitch leaves lead untouched");
    check (p.undoPitchEdit() && p.getPitchEdits()[0].harmonyTargets[0] == -1
        && p.redoPitchEdit() && p.getPitchEdits()[0].harmonyTargets[0] == 64,
        "harmony pitch participates in undo/redo");
    check (! p.setHarmonyEditTarget (101, 4, 64) && ! p.setHarmonyEditTarget (101, 0, 128),
        "invalid harmony edits rejected");
    auto& a = p.parameters();
    for (const auto* id : { "autoKey", "followSongKey", "harmonyHumanPitch", "harmonyHumanTime",
                           "harmonyEnsemble", "h1Delay", "h1Detune", "h1Pan", "h1Level", "doNoHarm" })
        setParameter (a, id, 0);
    for (const auto* id : { "liveMode", "usePitchEdits", "h1Enable", "harmonyVoiceOnly", "harmonyMix" })
        setParameter (a, id, 1);
    setParameter (a, "harmonyMode", 1);
    setParameter (a, "h1Degrees", 2);
    setParameter (a, "h1Formant", 7);
    setParameter (a, "keyRoot", 0);
    setParameter (a, "scale", 0);
    setParameter (a, "scaleMode", 0);
    juce::MemoryBlock saved; p.getStateInformation (saved);
    SattariAutoPitchAudioProcessor restored;
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    check (restored.getPitchEdits()[0].harmonyTargets[0] == 64
        && std::abs (restored.parameters().getRawParameterValue ("h1Formant")->load() - 7) < 0.01f,
        "project recall restores note overrides and formants");
    restored.setHarmonyEditTarget (101, 0, 65);
    juce::MemoryBlock resaved;
    restored.getStateInformation (resaved);
    auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
    const auto resavedTree = juce::ValueTree::fromXml (*resavedXml);
    int noteTrees = 0, keyTrees = 0;
    for (const auto& child : resavedTree)
    {
        if (child.hasType ("PitchEdits")) ++noteTrees;
        if (child.hasType ("KeyMap")) ++keyTrees;
    }
    restored.setStateInformation (resaved.getData(), (int) resaved.getSize());
    check (noteTrees == 1 && keyTrees == 1 && restored.getPitchEdits()[0].harmonyTargets[0] == 65,
        "repeated saves replace old graph/key-map snapshots");
    check (p.splitPitchEdit (101, 0.5) && p.getPitchEdits()[1].harmonyTargets[0] == 64
        && p.undoPitchEdit(), "split retains harmony targets");

    p.prepareToPlay (48000, 64);
    check (! p.harmonyFormantsAvailable(), "formants excluded from live latency path");
    TestPlayHead playhead; p.setPlayHead (&playhead);
    std::vector<float> output;
    double phase = 0;
    for (int offset = 0; offset < 96000; offset += 64)
    {
        juce::AudioBuffer<float> block (2, 64);
        for (int i = 0; i < 64; ++i)
        {
            const float sample = 0.15f * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
            block.setSample (0, i, sample); block.setSample (1, i, sample);
        }
        playhead.seconds = offset / 48000.0;
        juce::MidiBuffer midi; p.processBlock (block, midi);
        if (offset < 38400)
            output.insert (output.end(), block.getReadPointer (0), block.getReadPointer (0) + 64);
        if (offset == 32000)
            check (p.planHarmonyNow().active[0] && p.planHarmonyNow().midi[0] == 64,
                "audio-owned readout reports overridden H1");
    }
    check (std::abs (1200.0 * std::log2 (strongestNear (output, 24000, 48000, 329.63, 8) / 329.63)) < 35,
        "overridden H1 reaches actual audio output");
    check (p.planHarmonyNow().midi[0] == 60, "expired graph override returns to automatic third");
    auto raisedLead = p.getPitchEdits()[0];
    raisedLead.targetMidi = 69;
    p.setPitchEditEntry (101, raisedLead);
    p.setHarmonyEditTarget (101, 0, 76);
    output.clear();
    for (int offset = 0; offset < 48000; offset += 64)
    {
        juce::AudioBuffer<float> block (2, 64);
        for (int i = 0; i < 64; ++i)
        {
            const float sample = 0.15f * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
            block.setSample (0, i, sample); block.setSample (1, i, sample);
        }
        playhead.seconds = 0.5;
        juce::MidiBuffer midi; p.processBlock (block, midi);
        output.insert (output.end(), block.getReadPointer (0), block.getReadPointer (0) + 64);
    }
    const auto folded = p.planHarmonyNow();
    check (folded.midi[0] == 64 && p.getHarmonyEditOverlays()[0].midi == 64
        && tonePower (output, 24000, 48000, 329.63) > 2 * tonePower (output, 24000, 48000, 440.0),
        "large lead edits fold against source range; audio and graph retain E, not clipped A");
    raisedLead.targetMidi = 57;
    raisedLead.harmonyTargets[0] = 64;
    p.setPitchEditEntry (101, raisedLead);
    p.setPlayHead (nullptr);
    setParameter (a, "liveMode", 0);
    p.prepareToPlay (48000, 256);
   #if SATTARI_AUTOPITCH_HAS_RUBBERBAND
    check (p.harmonyFormantsAvailable(), "Studio has sufficient alignment for formants");
   #endif
    setParameter (a, "polyphonic", 1);
    check (! p.harmonyFormantsAvailable(), "Poly cannot advertise vocal formants");
    setParameter (a, "polyphonic", 0);

    for (bool live : { false, true })
    {
        setParameter (a, "liveMode", live ? 1 : 0);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (960, 760);
        check (clickButtonNamed (*editor, "HARMONY"), "harmony workspace opens at minimum size");
        std::function<juce::Component*(juce::Component&, const juce::String&)> find;
        find = [&] (juce::Component& parent, const juce::String& id) -> juce::Component*
        {
            if (parent.getComponentID() == id) return &parent;
            for (auto* child : parent.getChildren())
                if (auto* result = find (*child, id)) return result;
            return nullptr;
        };
        for (int v = 1; v <= 4; ++v)
        {
            auto* control = find (*editor, "h" + juce::String (v) + "Formant");
            check (control && control->isVisible() && control->getWidth() > 20
                && control->getHeight() > 10
                && editor->getLocalBounds().contains (editor->getLocalArea (control, control->getLocalBounds()))
                && control->isEnabled() == p.harmonyFormantsAvailable(),
                "formant slider fits minimum layout and reflects active engine");
        }
    }

    // Exercise the real timeline mouse handlers, including history gestures.
    PitchEditTimeline timeline;
    timeline.setSize (1000, 400);
    timeline.setData (p.getPitchEdits(), 0.5);
    timeline.setHarmonyOverlays (p.getHarmonyEditOverlays());
    timeline.onHarmonyChanged = [&] (juce::int64 id, int voice, int midi)
    { p.setHarmonyEditTarget (id, voice, midi); };
    timeline.onGestureBegin = [&] { p.beginPitchEditGesture(); };
    timeline.onGestureEnd = [&] { p.endPitchEditGesture(); };
    // Range: A3 minus 2 through E4 plus 2 = MIDI 55..66, 30px/row.
    auto event = [&] (float y, int modifiers, bool dragged)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 90, y },
            juce::ModifierKeys (modifiers), 1, 0, 0, 0, 0, &timeline, &timeline,
            juce::Time::getCurrentTime(), { 90, 305 }, juce::Time::getCurrentTime(), 1, dragged);
    };
    const float harmonyY = 380.0f - (64 - 55 + 0.5f) * 30.0f;
    timeline.mouseDown (event (harmonyY, juce::ModifierKeys::leftButtonModifier, false));
    timeline.mouseDrag (event (harmonyY - 30, juce::ModifierKeys::leftButtonModifier, true));
    timeline.mouseUp (event (harmonyY - 30, 0, true));
    check (p.getPitchEdits()[0].harmonyTargets[0] == 65 && p.getPitchEdits()[0].targetMidi == 57,
        "timeline drag edits H1 without moving lead");
    check (p.undoPitchEdit() && p.getPitchEdits()[0].harmonyTargets[0] == 64,
        "timeline drag is one reversible gesture");
    timeline.mouseDown (event (harmonyY, juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier, false));
    check (p.getPitchEdits()[0].harmonyTargets[0] == -2, "shift-click mutes harmony region");
    timeline.mouseDown (event (harmonyY, juce::ModifierKeys::rightButtonModifier, false));
    check (p.getPitchEdits()[0].harmonyTargets[0] == -1, "right-click restores generated harmony");

    // A lead bypass must not bypass the tracker needed by generated singers.
    for (int profile = 0; profile < 3; ++profile)
        for (bool correctionOff : { true, false })
        {
            SattariAutoPitchAudioProcessor bypass;
            auto& params = bypass.parameters();
            for (const auto* id : { "autoKey", "followSongKey", "neuralPitch", "harmonyHumanPitch",
                                   "harmonyHumanTime", "harmonyEnsemble", "h1Pan", "h1Detune", "h1Delay" })
                setParameter (params, id, 0);
            for (const auto* id : { "liveMode", "h1Enable", "harmonyVoiceOnly", "harmonyMix" })
                setParameter (params, id, 1);
            setParameter (params, "extremeLive", profile == 0 ? 1 : 0);
            setParameter (params, "liveProfile", profile == 2 ? 1 : 0);
            setParameter (params, "enableCorrection", correctionOff ? 0 : 1);
            setParameter (params, "amount", correctionOff ? 1 : 0);
            setParameter (params, "harmonyMode", 1);
            setParameter (params, "h1Degrees", 2);
            setParameter (params, "h1Level", 0);
            setParameter (params, "keyRoot", 0);
            setParameter (params, "scaleMode", 0);
            bypass.prepareToPlay (48000, 64);
            std::vector<float> rendered;
            for (int offset = 0; offset < 48000; offset += 64)
            {
                juce::AudioBuffer<float> block (2, 64);
                for (int i = 0; i < 64; ++i)
                {
                    const float sample = 0.15f * (float) std::sin (
                        juce::MathConstants<double>::twoPi * 220.0 * (offset + i) / 48000.0);
                    block.setSample (0, i, sample); block.setSample (1, i, sample);
                }
                juce::MidiBuffer midi; bypass.processBlock (block, midi);
                rendered.insert (rendered.end(), block.getReadPointer (0), block.getReadPointer (0) + 64);
            }
            const auto readout = bypass.planHarmonyNow();
            const double targetPower = tonePower (rendered, 24000, 48000, 261.626);
            const double sourcePower = tonePower (rendered, 24000, 48000, 220.0);
            std::cout << "    bypass harmony profile=" << profile << " correctionOff=" << correctionOff
                      << " targetPower=" << targetPower << " sourcePower=" << sourcePower << '\n';
            check (readout.valid && readout.active[0] && readout.midi[0] == 60
                && targetPower > 1.0 && targetPower > 2 * sourcePower,
                "Live harmony remains pitched with lead correction off or Amount zero");
        }
    return failures;
}
}
