#pragma once

static void mergeChecks (P& p, const juce::File& output)
{
    check (P::sourceSlots == 4, "Consolidated instrument provides four source layers");
    near (p.parameters.getParameter (P::macroId (0, "target1"))->getNormalisableRange().end, 80, "Original macro target range is unchanged");
    check (p.parameters.getParameter (P::nodeId (9, "velocity"))->getParameterIndex() == P::previousParameterCount - 1, "All 423 prior parameter positions remain unchanged");
    check (p.parameters.getParameter (P::nodeId (10, "type"))->getParameterIndex() == P::previousParameterCount, "Additional layers append after the old layout");
    for (const int type : { P::gate, P::face, P::glassChoir, P::transient, P::multiband, P::peakFx, P::chop })
        check (p.info (type, 0).valid, "Merged module exposes its actual DSP parameters");
    int choirControls = 0; for (int c = 0; c < P::allControls; ++c) if (p.info (P::glassChoir, c).valid) ++choirControls;
    check (choirControls > 32, "Glass Choir is not truncated to the old detail limit");
    check (p.info (P::granulator, 32).valid, "Granulator source details append without changing the old empty detail bank");
    check (p.info (P::chop, 1).text (.8f) == "80.0%", "Chop linear output displays as percentage, not decibels");
    check (P::collectionTypes (1).size() == 10 && P::collectionTypes (2).size() == 10 && P::collectionTypes (3).size() == 3 && P::collectionTypes (4).size() == 8, "Collection filters match the consolidated catalog");
    p.clearRack(); p.selectModule (5, P::heat); p.moveModule (5, 1);
    check (p.nodeType (5) == P::heat && p.nodeType (6) == P::empty, "Parallel routing preserves the main/return reorder boundary");
    p.set ("serial", 1); p.moveModule (5, 1);
    check (p.nodeType (5) == P::empty && p.nodeType (6) == P::heat, "Eight-serial routing allows movement across the former return boundary");
    for (int n = 0; n < 6; ++n) p.selectModule (n, P::eq);
    check (p.duplicateModule (6) && p.nodeType (7) == P::heat, "Eight-serial duplication can use the last slot");
    p.loadFactory (0); juce::MemoryBlock saved; p.getStateInformation (saved);
    auto old = juce::ValueTree::readFromData (saved.getData(), saved.getSize()); old.setProperty ("version", 3, nullptr);
    const auto strip = [&p] (juce::ValueTree rack)
    {
        auto values = rack.getChildWithName ("MODULAR_VALUES");
        for (int i = values.getNumChildren(); --i >= 0;)
            if (p.parameters.getParameter (values.getChild (i)["id"].toString())->getParameterIndex() >= P::previousParameterCount) values.removeChild (i, nullptr);
    };
    strip (old.getChildWithName ("RACK"));
    for (const auto* group : { "SCENES", "COMPARISONS" }) for (auto rack : old.getChildWithName (group)) strip (rack);
    juce::MemoryOutputStream legacy; old.writeToStream (legacy);
    p.selectModule (11, P::chop); p.set ("serial", 1);
    p.setStateInformation (legacy.getData(), (int) legacy.getDataSize());
    check (p.lastError().isEmpty() && p.nodeType (10) == P::empty && p.nodeType (11) == P::empty, "v3 state migrates with new layers empty");
    near (p.value ("serial"), 0, "v3 migration preserves the parallel return");
    p.clearRack(); p.set ("workspace", 2); p.selectModule (10, P::chop); p.selectModule (11, P::granulator);
    p.setMappingTarget (0, 0, 10 * P::controls + 1); p.set (P::macroId (0), .7f);
    check (p.isMapped (10, 0), "Additional layers can be macro targets");
    p.moveModule (10, 1);
    check (p.mappingTarget (0, 0) == 11 * P::controls + 1 && p.nodeType (11) == P::chop, "Reorder moves new-layer macro assignments");
    juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi;
    p.reset(); midi.addEvent (juce::MidiMessage::noteOn (1, 36, .8f), 0); p.processBlock (audio, midi);
    float peak = 0;
    for (int block = 0; block < 100; ++block) { process (p, audio, 1); peak = juce::jmax (peak, audio.getMagnitude (0, 256)); }
    check (peak > .0001f, "Chop produces audio in the fourth layer");
    p.getStateInformation (saved); p.clearRack(); p.setStateInformation (saved.getData(), (int) saved.getSize());
    check (p.lastError().isEmpty() && p.nodeType (11) == P::chop, "Four-layer v4 host state recalls successfully");
    for (int layer = 0; layer < P::sourceSlots; ++layer)
    {
        juce::AudioBuffer<float> source (1, 4096);
        for (int s = 0; s < source.getNumSamples(); ++s) source.setSample (0, s, .1f * std::sin ((float) s * .05f * (float) (layer + 1)));
        const auto file = output.getChildFile ("merge-layer-" + juce::String (layer) + ".wav"); writeWav (file, source);
        check (p.loadSample (layer, file).wasOk(), "Each of four layers accepts a distinct source sample");
    }
    p.moveModule (10, 1);
    check (p.sampleName (2) == "merge-layer-3.wav" && p.sampleName (3) == "merge-layer-2.wav", "New-layer reorder swaps actual source ownership");
    p.undo(); check (p.sampleName (3) == "merge-layer-3.wav", "New-layer undo restores source ownership");
    p.getStateInformation (saved); p.clearRack(); p.setStateInformation (saved.getData(), (int) saved.getSize());
    check (p.lastError().isEmpty() && p.sampleName (0) == "merge-layer-0.wav" && p.sampleName (3) == "merge-layer-3.wav", "Four embedded source samples survive host recall");
    if (! SATTARI_MODULAR_SYNTH)
    {
        auto* document = new juce::DynamicObject(); document->setProperty ("schemaVersion", 1); document->setProperty ("product", "vocal");
        auto* values = new juce::DynamicObject(); document->setProperty ("parameters", juce::var (values));
        for (int n = 1; n <= 8; ++n) { values->setProperty ("slot" + juce::String (n), n == 1 || n == 2 ? 8 : n == 8 ? 10 : 0); values->setProperty ("slot" + juce::String (n) + "bypass", 0); }
        values->setProperty ("comp.threshold", -24.0); values->setProperty ("comp.autoRelease", 1.0);
        const juce::var preset (document); const auto file = output.getChildFile ("vocal-import.sattaripreset");
        check (file.replaceWithText (juce::JSON::toString (preset)), "Legacy import fixture saved");
        check (p.importLegacyPreset (file).wasOk(), "Legacy Vocal settings import");
        check (p.nodeType (0) == P::comp && p.nodeType (1) == P::comp && p.nodeType (7) == P::space, "Eight-slot chain order is retained");
        near (p.value ("serial"), 1, "Legacy import uses eight serial effects");
        near (p.value (P::nodeId (1, "bypass")), 1, "Legacy inactive duplicates stay inactive");
        near (p.info (P::comp, 0).range.convertFrom0to1 (p.value (P::controlId (0, 0))), -24, "Legacy real parameter values convert correctly");
        const auto before = p.presetName();
        values->setProperty ("mod1.target", 1); values->setProperty ("mod1.depth", .2);
        file.replaceWithText (juce::JSON::toString (preset));
        check (p.importLegacyPreset (file).failed() && p.presetName() == before, "Unsupported modulation is rejected without changing the rack");
        p.undo(); check (p.nodeType (11) == P::chop, "One undo restores the pre-import instrument and its sources");
        values->removeProperty ("mod1.target"); values->removeProperty ("mod1.depth");
        document->setProperty ("product", "create");
        for (int n = 1; n <= 8; ++n) values->setProperty ("slot" + juce::String (n), n == 1 ? 1 : n == 8 ? 10 : 0);
        values->setProperty ("echo.timems", 345.0);
        file.replaceWithText (juce::JSON::toString (preset));
        check (p.importLegacyPreset (file).wasOk() && p.nodeType (0) == P::echo && p.nodeType (7) == P::bass808, "Create preset imports through the eighth serial effect");
        near (p.info (P::echo, 0).range.convertFrom0to1 (p.value (P::controlId (0, 0))), 345, "Create delay time preserves the actual value");
        juce::MemoryBlock beforeFailure; p.getStateInformation (beforeFailure);
        const auto valid = juce::JSON::toString (preset);
        for (const auto& invalid : { valid + "{}", valid.dropLastCharacters (1), juce::String ("{\"extra\":[[[[[[[[[0]]]]]]]]]}"), juce::String ("{\"schemaVersion\":1,\"product\":\"create\",\"parameters\":{\"slot1\":\"bad\"}}") })
        {
            file.replaceWithText (invalid); check (p.importLegacyPreset (file).failed(), "Malformed legacy preset is rejected");
            juce::MemoryBlock afterFailure; p.getStateInformation (afterFailure);
            check (afterFailure == beforeFailure, "Rejected import leaves the complete state unchanged");
        }
    }
    p.loadFactory (11);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor()); editor->setSize (1120, 820);
    inspectLayout (*editor);
    auto* collection = findControl<juce::ComboBox> (*editor, "Module collection");
    check (collection != nullptr && collection->getNumItems() == 6, "Native collection selector exposes all curated views");
    check (sattari::test::renderComponentToPng (*editor, output.getChildFile ("modular-four-layers.png")), "Four-layer instrument renders at minimum size");
    editor.reset(); p.clearRack(); p.set ("workspace", 0);
    if (SATTARI_MODULAR_SYNTH) p.selectModule (8, P::granulator);
    for (const int type : { P::gate, P::face, P::glassChoir, P::transient, P::multiband, P::peakFx })
    {
        p.selectModule (0, type); p.reset();
        midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 60, .8f), 0); audio.clear(); p.processBlock (audio, midi);
        process (p, audio, 40, true);
        check (p.getLatencySamples() >= 0 && p.getLatencySamples() < 9 * 16384, "Merged native effect processes finite audio with bounded latency");
    }
    p.selectModule (0, P::glassChoir);
    editor.reset (p.createEditor()); editor->setSize (1120, 820);
    auto* details = findControl<juce::Button> (*editor, "Detailed effect controls");
    check (details != nullptr, "Glass Choir detailed controls are available"); details->onClick(); inspectLayout (*editor);
    check (sattari::test::renderComponentToPng (*editor, output.getChildFile ("modular-choir-details.png")), "Merged Glass Choir controls render inside their row");
}
