#pragma once

static void upgradeChecks (P& p, const juce::File& output)
{
    check (p.info (P::bass808, 7).text (.5f) == "100.0%", "808 output is displayed as linear gain, not decibels");
    auto* type = p.parameters.getParameter (P::nodeId (0, "type"));
    near (type->getNormalisableRange().end, (float) P::rhythm, "Legacy module automation range is frozen");
    check (p.parameters.getParameter ("workspace")->getParameterIndex() == P::legacyParameterCount - 1, "New parameters append after all legacy indices");
    p.clearRack(); p.selectModule (0, P::doubleFx); p.set (P::controlId (0, 0), .8f);
    check (p.nodeType (0) == P::doubleFx && p.value (P::nodeId (0, "type")) < .00001f, "Extended modules do not change the legacy type range");
    check (p.duplicateModule (0), "Double has independent duplicate instances");
    p.set (P::controlId (1, 0), .1f); near (p.value (P::controlId (0, 0)), .8f, "Double duplicate controls do not leak");
    p.moveModule (0, 1); near (p.value (P::controlId (1, 0)), .8f, "Extended module moves preserve values");
    p.undo(); near (p.value (P::controlId (0, 0)), .8f, "Extended module undo");
    p.selectModule (0, P::comp);
    int autoRelease = -1;
    for (int c = P::controls; c < P::allControls; ++c) if (p.info (P::comp, c).parameterId == "autoRelease") autoRelease = c;
    check (autoRelease >= P::controls, "Detailed controls expose compressor auto release");
    p.set (P::controlId (0, autoRelease), 1);
    juce::MemoryBlock saved; p.getStateInformation (saved); p.clearRack(); p.setStateInformation (saved.getData(), (int) saved.getSize());
    near (p.value (P::controlId (0, autoRelease)), 1, "Detailed controls survive host recall");
    p.moveModule (0, 1); near (p.value (P::controlId (1, autoRelease)), 1, "Detailed controls follow module moves");
    p.undo(); near (p.value (P::controlId (0, autoRelease)), 1, "Detailed controls follow undo");

    // Reconstruct the exact v1 field set, including all inactive scene/A-B states.
    p.loadFactory (0); p.getStateInformation (saved);
    auto legacy = juce::ValueTree::readFromData (saved.getData(), saved.getSize());
    legacy.setProperty ("version", 1, nullptr);
    auto strip = [&] (juce::ValueTree rack)
    {
        auto values = rack.getChildWithName ("MODULAR_VALUES");
        for (int i = values.getNumChildren(); --i >= 0;)
        {
            auto* parameter = p.parameters.getParameter (values.getChild (i)["id"].toString());
            if (parameter->getParameterIndex() >= P::legacyParameterCount) values.removeChild (i, nullptr);
        }
        check (values.getNumChildren() == P::legacyParameterCount, "Legacy fixture has exact v1 parameter count");
    };
    strip (legacy.getChildWithName ("RACK"));
    for (const auto* group : { "SCENES", "COMPARISONS" }) for (auto rack : legacy.getChildWithName (group)) strip (rack);
    juce::MemoryOutputStream old; legacy.writeToStream (old);
    p.set ("protect", 1); p.setStateInformation (old.getData(), (int) old.getDataSize());
    check (p.lastError().isEmpty(), "Version 1 presets migrate successfully");
    near (p.value ("protect"), 0, "Legacy migration disables new safety processing");
    p.selectScene (1); check (p.nodeType (0) == P::clean, "Legacy inactive scenes migrate");
    for (int c = P::controls; c < P::allControls; ++c)
        near (p.value (P::controlId (2, c)), p.info (P::comp, c).initial, "Legacy detailed controls restore module defaults");

    p.clearRack(); p.set ("workspace", 0); p.set ("output", 12); p.set ("protect", 1);
    juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi;
    bool bounded = true;
    for (int block = 0; block < 40; ++block)
    {
        for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 2.0f, 256);
        p.processBlock (audio, midi);
        bounded = bounded && audio.getMagnitude (0, 256) <= .980001f;
    }
    check (bounded, "Optional safety clip bounds output");
    p.loadFactory (5); check (p.nodeType (0) == P::warble && p.nodeType (1) == P::echo, "Create collection preset loads native effects");
    p.loadFactory (4); check (p.nodeType (1) == P::doubleFx, "Vocal collection preset loads native Double");
    p.clearRack(); p.set ("workspace", 2); p.selectModule (8, P::granulator);
    p.set (P::nodeId (8, "keyLow"), 72); p.reset();
    midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 60, .8f), 0); audio.clear(); p.processBlock (audio, midi);
    process (p, audio, 20);
    check (audio.getMagnitude (0, 256) < .000001f, "Instrument key range rejects out-of-zone notes");
    p.set (P::nodeId (8, "keyLow"), 0); p.set (P::nodeId (8, "pan"), 1); p.reset();
    midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 60, .8f), 0); audio.clear(); p.processBlock (audio, midi);
    float left = 0, right = 0;
    process (p, audio, 8);
    for (int block = 0; block < 80; ++block)
    { process (p, audio, 1); left += audio.getRMSLevel (0, 0, 256); right += audio.getRMSLevel (1, 0, 256); }
    std::cout << "LAYER pan render left=" << left << " right=" << right << '\n';
    check (right > .01f && left < .000001f, "Instrument layer pan reaches audio output");
    p.set (P::nodeId (8, "transpose"), 12); p.set (P::nodeId (8, "velocity"), 2);
    check (p.duplicateModule (8), "Instrument layer duplicates with routing");
    near (p.value (P::nodeId (9, "transpose")), 12, "Duplicate preserves layer transpose");
    near (p.value (P::nodeId (9, "velocity")), 2, "Duplicate preserves velocity curve");
    p.set (P::nodeId (8, "transpose"), 0); p.moveModule (8, 1);
    near (p.value (P::nodeId (8, "transpose")), 12, "Layer mapping follows reorder");
    p.getStateInformation (saved); p.setStateInformation (saved.getData(), (int) saved.getSize());
    near (p.value (P::nodeId (8, "transpose")), 12, "Layer mapping survives host state");
    if (! SATTARI_MODULAR_SYNTH)
    {
        for (const int slot : { 0, 7 })
        {
            juce::AudioBuffer<float> first (2, 256);
            for (int pass = 0; pass < 2; ++pass)
            {
                p.clearRack(); p.set ("workspace", 0); p.selectModule (slot, P::vocoder);
                if (slot == 7) { p.selectModule (6, P::vocoder); p.set (P::nodeId (6, "bypass"), 1); p.set ("return", 1); }
                p.reset();
                for (int block = 0; block < 120; ++block)
                {
                    for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < 256; ++s)
                        audio.setSample (ch, s, .2f * std::sin ((float) (s + block * 256) * .0575958653f));
                    midi.clear(); if (block == 0) midi.addEvent (juce::MidiMessage::noteOn (1, pass == 0 ? 60 : 72, .8f), 0);
                    p.processBlock (audio, midi);
                }
                if (pass == 0) first.makeCopyOf (audio);
            }
            float difference = 0;
            for (int s = 0; s < 256; ++s) difference += std::abs (audio.getSample (0, s) - first.getSample (0, s));
            check (difference > .001f, "MIDI notes audibly control the Vocoder in serial and return paths");
        }
    }
    p.loadFactory (4);
    p.selectModule (0, P::comp);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* details = findControl<juce::Button> (*editor, "Detailed effect controls");
    check (details != nullptr, "Detailed view control exists"); details->onClick();
    auto* advanced = findControl<juce::Button> (*editor, p.info (P::comp, autoRelease).name);
    check (advanced && advanced->isVisible() && advanced->getParentComponent()->getLocalBounds().contains (advanced->getBounds()), "Expanding a module immediately exposes advanced controls inside its row");
    editor->setSize (1120, 820); inspectLayout (*editor);
    check (sattari::test::renderComponentToPng (*editor, output.getChildFile ("modular-details.png")), "Detailed controls render at minimum size");
    editor.reset(); p.set ("workspace", 2); p.selectModule (8, P::granulator);
    editor.reset (p.createEditor()); editor->setSize (1120, 820);
    auto* layer = findControl<juce::Button> (*editor, "Layer controls");
    check (layer != nullptr, "Layer editor control exists"); layer->onClick();
    auto* transpose = findControl<juce::Slider> (*editor, "Transpose / st");
    check (transpose != nullptr, "Layer mapping editor opens");
    auto* callout = transpose->findParentComponentOfClass<juce::CallOutBox>();
    check (callout && callout->getParentComponent() == editor.get(), "Layer popup overlays the editor without rack-row clipping");
    transpose->setValue (7, juce::sendNotificationSync);
    near (p.value (P::nodeId (8, "transpose")), 7, "Layer editor writes the host parameter");
    inspectLayout (*editor);
    check (sattari::test::renderComponentToPng (*editor, output.getChildFile ("modular-layer.png")), "Layer controls render at minimum size");
    editor.reset(); check (true, "Editor closes safely with its layer popup open");
    std::cout << "BASELINE latency=" << p.getLatencySamples() << " samples; parameters=" << p.getParameters().size() << '\n';
}
