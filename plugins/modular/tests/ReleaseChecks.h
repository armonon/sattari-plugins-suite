#pragma once
#include "StateReader.h"
#include <chrono>
#include <cstdlib>
#include <new>

namespace allocationProbe
{
thread_local bool armed = false;
thread_local size_t count = 0;
static void* allocate (size_t size)
{
    if (armed) ++count;
    return std::malloc (size > 0 ? size : 1);
}
}
void* operator new (size_t size) { if (auto* p = allocationProbe::allocate (size)) return p; throw std::bad_alloc(); }
void* operator new[] (size_t size) { return ::operator new (size); }
void* operator new (size_t size, const std::nothrow_t&) noexcept { return allocationProbe::allocate (size); }
void* operator new[] (size_t size, const std::nothrow_t&) noexcept { return allocationProbe::allocate (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, size_t) noexcept { std::free (p); }
void operator delete[] (void* p, size_t) noexcept { std::free (p); }

namespace
{
juce::MemoryBlock stateOf (P& p)
{ juce::MemoryBlock state; p.getStateInformation (state); return state; }

void releaseChecks (P& p, const juce::File& output)
{
    auto* bypass = p.parameters.getParameter (P::nodeId (1, "bypass"));
    bypass->setValueNotifyingHost (0.47f);
    near (bypass->getValue(), 0, "Host boolean automation reports a canonical off value");
    auto booleanState = stateOf (p); bypass->setValueNotifyingHost (0.84f);
    near (bypass->getValue(), 1, "Host boolean automation reports a canonical on value");
    p.setStateInformation (booleanState.getData(), (int) booleanState.getSize());
    near (bypass->getValue(), 0, "Fractional boolean automation restores correctly");
    p.clearRack(); p.set ("workspace", 2); p.selectModule (8, P::granulator); p.selectModule (9, P::rhythm);
    juce::AudioBuffer<float> audio (1, 2048);
    for (int i = 0; i < audio.getNumSamples(); ++i) audio.setSample (0, i, 0.1f * std::sin ((float) i * 0.08f));
    const auto a = output.getChildFile ("source-a.wav"), b = output.getChildFile ("source-b.wav");
    writeWav (a, audio); audio.applyGain (0.5f); writeWav (b, audio);
    check (p.loadSample (0, a).wasOk() && p.loadSample (1, b).wasOk(), "Distinct sources load for both layers");
    check (! p.canUndo(), "Sample import starts a new bounded undo session");
    p.moveModule (8, 1);
    check (p.sampleName (1) == a.getFileName() && p.sampleName (0) == b.getFileName(), "Layer reorder moves the actual source sample");
    p.undo(); check (p.sampleName (0) == a.getFileName(), "Undo restores source ownership");
    p.redo(); check (p.sampleName (1) == a.getFileName(), "Redo restores source ownership");
    for (int i = 0; i < 45; ++i) p.moveModule (8, 1);
    int undoCount = 0; while (p.canUndo()) { p.undo(); ++undoCount; }
    check (undoCount == 40, "Sample reorder undo history remains bounded");
    while (p.canRedo()) p.redo();
    p.moveModule (8, 1);
    p.selectModule (8, P::empty); check (p.duplicateModule (9), "Instrument duplication succeeds");
    check (p.sampleName (0) == a.getFileName(), "Instrument duplication copies its loaded sample");
    auto state = stateOf (p);
    auto tree = sattari::modular::StateReader::read (state.getData(), state.getSize());
    check (tree.isValid(), "Bounded reader accepts native state");
    juce::MemoryBlock firstPcm;
    int sourceCount = 0;
    for (auto child : tree) if (child.hasType ("SAMPLE"))
    {
        const auto pcm = *child["pcm"].getBinaryData();
        if (sourceCount++ == 0) firstPcm = pcm;
        else check (firstPcm == pcm, "Duplicated layers embed identical PCM");
    }
    check (sourceCount == 2, "Both source payloads are saved");
    p.setStateInformation (state.getData(), (int) state.getSize());
    check (p.lastError().isEmpty() && p.sampleName (0) == a.getFileName() && p.sampleName (1) == a.getFileName(), "Copied source ownership survives host recall");
    a.deleteFile(); b.deleteFile();
    p.loadFactory (2);
    check (p.sampleName (0) == "Factory harmonic source", "Factory presets restore their factory source");
    p.undo(); check (p.sampleName (0) == "source-a.wav", "Undo factory load restores imported source");
    p.selectModule (0, P::heat);
    p.set (P::macroId (0, "target1"), 1); p.set (P::macroId (0, "low1"), 0.2f); p.set (P::macroId (0, "high1"), 0.8f); p.set (P::macroId (0), 0.5f);
    check (p.duplicateModule (0), "Macro-driven module duplicates");
    near (p.value (P::controlId (1, 0)), 0.5f, "Duplicate captures the current macro-driven setting independently");

    const auto reject = [&p] (const juce::MemoryBlock& invalid)
    {
        const auto before = stateOf (p);
        p.setStateInformation (invalid.getData(), (int) invalid.getSize());
        if (p.lastError().isEmpty() || before != stateOf (p)) throw std::runtime_error ("Malformed state changed the live rack");
    };
    for (size_t length = 1; length < state.getSize(); length += juce::jmax ((size_t) 1, state.getSize() / 97))
        reject (juce::MemoryBlock (state.getData(), length));
    check (true, "Truncated states are rejected without changing the rack");
    auto trailing = state; trailing.append ("x", 1); reject (trailing);
    check (true, "Trailing preset bytes are rejected");
    const auto rejectTree = [&reject] (const juce::ValueTree& invalid)
    { juce::MemoryOutputStream bytes; invalid.writeToStream (bytes); reject (bytes.getMemoryBlock()); };
    auto bad = tree.createCopy(); bad.setProperty ("scene", 99, nullptr); rejectTree (bad);
    bad = tree.createCopy(); bad.addChild (bad.getChildWithName ("RACK").createCopy(), -1, nullptr); rejectTree (bad);
    bad = tree.createCopy(); bad.getChildWithName ("RACK").getChildWithName ("MODULAR_VALUES").getChild (0).setProperty ("value", "invalid", nullptr); rejectTree (bad);
    bad = tree.createCopy(); bad.getChildWithName ("RACK").getChildWithName ("MODULAR_VALUES").getChildWithProperty ("id", P::nodeId (0, "type")).setProperty ("value", 0.5, nullptr); rejectTree (bad);
    bad = tree.createCopy(); bad.getChildWithName ("RACK").getChildWithName ("MODULAR_VALUES").getChildWithProperty ("id", P::nodeId (0, "type")).setProperty ("value", (int) P::typeCount, nullptr); rejectTree (bad);
    bad = tree.createCopy(); auto nested = bad;
    for (int i = 0; i < 20; ++i) { juce::ValueTree child ("NESTED"); nested.addChild (child, -1, nullptr); nested = child; }
    rejectTree (bad);
    check (true, "Invalid selections, types, duplicate sections, and deep trees are rejected");
    juce::MemoryOutputStream huge;
    huge.writeString ("SATTARI_MODULAR"); huge.writeCompressedInt (0); huge.writeCompressedInt (0x7fffffff);
    reject (huge.getMemoryBlock());
    huge.reset(); huge.writeString ("SATTARI_MODULAR"); huge.writeCompressedInt (1); huge.writeString ("pcm");
    huge.writeCompressedInt (0x7fffffff); huge.writeByte (8); reject (huge.getMemoryBlock());
    check (true, "Untrusted allocation sizes are rejected before JUCE decoding");
    juce::Random random (0x53415452);
    for (int i = 0; i < 2000; ++i)
    {
        juce::MemoryBlock noise ((size_t) random.nextInt (4096) + 1); random.fillBitsRandomly (noise.getData(), noise.getSize());
        if (sattari::modular::StateReader::read (noise.getData(), noise.getSize()).isValid()) throw std::runtime_error ("Random state unexpectedly parsed");
    }
    check (true, "2000 malformed binary inputs terminate safely");
    const auto legacy = juce::File (__FILE__).getParentDirectory().getChildFile ("fixtures/v1.sattarimodular");
    check (legacy.existsAsFile() && p.loadPreset (legacy).wasOk(), "0.1.0 preset compatibility");

    p.clearRack(); p.set ("workspace", 2); p.selectModule (8, P::granulator); p.prepareToPlay (48000, 256);
    juce::AudioBuffer<float> block (2, 256); juce::MidiBuffer midi;
    p.queueNote (60, 0.7f, true); p.reset(); process (p, block, 100);
    check (block.getMagnitude (0, 256) < 0.0001f, "Host reset discards queued keyboard notes");
    p.queueNote (60, 0.7f, true); process (p, block, 60);
    p.allNotesOff(); process (p, block, 400);
    check (block.getMagnitude (0, 256) < 0.0001f, "Emergency all-notes-off releases voices");
    p.queueNote (60, 0.7f, true); process (p, block, 60);
    state = stateOf (p); p.setStateInformation (state.getData(), (int) state.getSize()); process (p, block, 400);
    check (block.getMagnitude (0, 256) < 0.0001f, "Host state recall cannot retain a stale held voice");
    for (int i = 0; i < 5000; ++i) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.5f), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 1); block.clear(); p.processBlock (block, midi); process (p, block, 400);
    check (block.getMagnitude (0, 256) < 0.0001f, "Dense host MIDI overflow cannot strand a voice");

    for (int type = 1; type < P::typeCount; ++type)
    {
        p.clearRack(); p.set ("workspace", type >= P::granulator ? 2 : 0);
        if (SATTARI_MODULAR_SYNTH) p.selectModule (8, P::granulator);
        const int node = type >= P::granulator ? 8 : 0; p.selectModule (node, type);
        p.queueNote (60, 0.5f, true); process (p, block, 16, true);
        allocationProbe::count = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < 80; ++iteration)
        {
            for (int control = 0; control < P::controls; ++control)
                p.set (P::controlId (node, control), iteration % 3 == 0 ? 0 : iteration % 3 == 1 ? 1 : random.nextFloat(), false);
            for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < 256; ++s) block.setSample (ch, s, 0.15f * std::sin ((float) (s + iteration * 256) * 0.1f));
            allocationProbe::armed = true;
            p.processBlock (block, midi);
            allocationProbe::armed = false;
            for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < 256; ++s)
                if (! std::isfinite (block.getSample (ch, s))) throw std::runtime_error ("Automation produced non-finite output");
        }
        const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::cout << "PROFILE " << P::typeName (type) << " 80 blocks wall-seconds=" << elapsed << " C++ allocations=" << allocationProbe::count << std::endl;
        check (allocationProbe::count == 0, ("Allocation-free C++ processing under automation: " + P::typeName (type)).toRawUTF8());
    }
    p.loadFactory (0); process (p, block, 16, true);
    if (! SATTARI_MODULAR_SYNTH)
    {
        block.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
        block.setSample (1, 1, std::numeric_limits<float>::infinity());
        p.processBlock (block, midi); process (p, block, 80, true);
        check (block.getMagnitude (0, 256) > 0.001f, "Audio recovers after non-finite host input");
    }
    p.setNonRealtime (true); process (p, block, 64, true); p.setNonRealtime (false);
    check (true, "Offline render path remains finite");
}
}
