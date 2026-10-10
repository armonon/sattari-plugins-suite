#include "PluginProcessor.h"
#include "HostedPlugins.h"
#include <sattari_audio_core/SattariEditorRender.h>
#include <iostream>
#include <stdexcept>

using P = SattariModularProcessor;
namespace
{
void require (bool value, const char* message)
{ if (! value) throw std::runtime_error (message); std::cout << "PASS " << message << std::endl; }
void near (float a, float b, const char* message) { require (std::abs (a - b) < .001f, message); }
void settle (P& p, bool input = false)
{
    juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer midi;
    for (int i = 0; i < 20; ++i) { b.clear(); if (input) for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < 256; ++s) b.setSample (ch, s, .2f); p.processBlock (b, midi); }
}
template <typename T> T* find (juce::Component& component, const juce::String& title)
{
    if (auto* result = dynamic_cast<T*> (&component); result && result->isVisible() && result->getTitle() == title) return result;
    for (auto* child : component.getChildren()) if (auto* result = find<T> (*child, title)) return result;
    return nullptr;
}
int smoke (const juce::File& effect, const juce::File& instrument, const juce::File& output)
{
    P p; p.clearRack(); p.set ("workspace", 2); p.prepareToPlay (48000, 256);
    juce::String error;
    auto effects = p.scanHostedPlugin (effect, false, error); require (! effects.isEmpty(), error.isEmpty() ? "Installed effect scanned" : error.toRawUTF8());
    auto instruments = p.scanHostedPlugin (instrument, true, error); require (! instruments.isEmpty(), error.isEmpty() ? "Installed instrument scanned" : error.toRawUTF8());
    std::cout << "SMOKE " << instruments[0].name << " -> " << effects[0].name << std::endl;
    require (p.loadHostedPlugin (8, instruments[0]).wasOk() && p.loadHostedPlugin (0, effects[0]).wasOk(), "Installed plugin requests accepted");
    p.serviceHostedChanges();
    require (p.hostedInstance (8) != nullptr, p.hostedStatus (8).toRawUTF8());
    require (p.hostedInstance (0) != nullptr, p.hostedStatus (0).toRawUTF8());
    settle (p); p.queueNote (60, .7f, true);
    juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer midi; double energy = 0;
    for (int block = 0; block < 400; ++block)
    {
        b.clear(); p.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch) for (int sample = 0; sample < 256; ++sample)
        { const float value = b.getSample (ch, sample); if (! std::isfinite (value)) throw std::runtime_error ("Non-finite installed-plugin output"); energy += value * value; }
    }
    require (energy > .001, "Installed instrument and effect produce audible output");
    juce::MemoryBlock state; p.getStateInformation (state);
    p.setStateInformation (state.getData(), (int) state.getSize()); p.serviceHostedChanges();
    require (p.lastError().isEmpty() && p.hostedInstance (8) && p.hostedInstance (0), "Installed plugins reopen from saved state");
    settle (p); p.queueNote (60, .7f, true); double restoredEnergy = 0;
    for (int block = 0; block < 100; ++block)
    { b.clear(); p.processBlock (b, midi); restoredEnergy += b.getRMSLevel (0, 0, 256); }
    require (std::isfinite (restoredEnergy) && restoredEnergy > .001, "Reopened installed chain renders audio");
    output.createDirectory();
    require (sattari::test::renderEditorToPng (p, output.getChildFile ("installed-hosting.png"), 1120, 820), "Installed hosting UI rendered");
    p.releaseResources(); return 0;
}
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        if (argc == 5 && juce::String (argv[1]) == "--smoke") return smoke (juce::File (argv[2]), juce::File (argv[3]), juce::File (argv[4]));
        require (argc == 4, "Fixture paths supplied");
        P p; p.clearRack(); p.prepareToPlay (48000, 64); p.set ("workspace", 2);
        juce::String error;
        auto effects = p.scanHostedPlugin (juce::File (argv[1]), false, error);
        auto instruments = p.scanHostedPlugin (juce::File (argv[2]), true, error);
        require (effects.size() == 1 && instruments.size() == 1, "Real VST3 effect and instrument scanned");
        require (p.scanHostedPlugin (juce::File (argv[1]), true, error).isEmpty(), "Instrument browser rejects an effect");
        require (p.loadHostedPlugin (8, effects[0]).failed(), "Wrong slot type rejected");
        auto recursive = effects[0]; recursive.name = "Sattari Modular FX";
        require (p.loadHostedPlugin (0, recursive).failed(), "Recursive Modular hosting rejected");
        require (p.loadHostedPlugin (8, instruments[0]).wasOk() && p.loadHostedPlugin (0, effects[0]).wasOk(), "Hosting requests accepted");
        p.serviceHostedChanges();
        require (p.hostedInstance (8) && p.hostedInstance (0), "Both hosted plugins instantiated");
        settle (p); p.queueNote (60, .8f, true); settle (p);
        juce::AudioBuffer<float> b (2, 1024); juce::MidiBuffer midi; b.clear(); p.processBlock (b, midi);
        near (b.getSample (0, 900), .035f, "Instrument MIDI reaches effect through oversized block chunks");
        require (p.getLatencySamples() == 128 + 64 + 7 * 6, "Instrument and serial effect latencies reported");
        p.hostedInstance (0)->getParameters()[0]->setValueNotifyingHost (.8f);
        require (p.duplicateModule (0), "Hosted effect duplicated"); p.serviceHostedChanges();
        require (p.hostedInstance (0) != p.hostedInstance (1), "Duplicates own independent instances");
        near (p.hostedInstance (1)->getParameters()[0]->getValue(), .8f, "Duplicate copies plugin state");
        p.hostedInstance (1)->getParameters()[0]->setValueNotifyingHost (.25f);
        near (p.hostedInstance (0)->getParameters()[0]->getValue(), .8f, "Duplicate parameters remain independent");
        p.moveModule (0, 1);
        near (p.hostedInstance (1)->getParameters()[0]->getValue(), .8f, "Moving slots moves hosted instances");
        p.undo(); p.serviceHostedChanges(); near (p.hostedInstance (0)->getParameters()[0]->getValue(), .8f, "Undo restores hosted ownership and state");
        p.copyAB(); p.selectAB (1); p.serviceHostedChanges();
        p.hostedInstance (0)->getParameters()[0]->setValueNotifyingHost (.4f);
        p.selectAB (0); p.serviceHostedChanges(); near (p.hostedInstance (0)->getParameters()[0]->getValue(), .8f, "A/B restores hosted plugin state");
        juce::MemoryBlock state; p.getStateInformation (state);
        P recalled; recalled.prepareToPlay (44100, 256); recalled.setStateInformation (state.getData(), (int) state.getSize()); recalled.serviceHostedChanges();
        require (recalled.lastError().isEmpty() && recalled.hostedInstance (8), "Session reopens with hosted instrument");
        near (recalled.hostedInstance (1)->getParameters()[0]->getValue(), .25f, "Session restores independent effect state");
        auto tree = juce::ValueTree::readFromData (state.getData(), state.getSize());
        auto missing = tree.getChildWithName ("RACK").getChildWithName ("HOSTED").getChildWithProperty ("node", 0);
        auto description = effects[0]; description.fileOrIdentifier = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("missing-host-fixture.vst3").getFullPathName();
        missing.setProperty ("description", description.createXml()->toString(), nullptr);
        juce::MemoryOutputStream broken; tree.writeToStream (broken);
        recalled.setStateInformation (broken.getData(), (int) broken.getDataSize()); recalled.serviceHostedChanges();
        require (! recalled.hostedInstance (0) && recalled.nodeType (0) == P::externalEffect, "Missing plugin retains a visible placeholder");
        juce::MemoryBlock missingState; recalled.getStateInformation (missingState);
        auto missingSaved = juce::ValueTree::readFromData (missingState.getData(), missingState.getSize()).getChildWithName ("RACK").getChildWithName ("HOSTED").getChildWithProperty ("node", 0);
        require (missingSaved["data"].getBinaryData() != nullptr, "Missing plugin retains its opaque saved state");
        require (recalled.loadHostedPlugin (0, effects[0]).wasOk(), "Missing plugin can be relinked"); recalled.serviceHostedChanges();
        near (recalled.hostedInstance (0)->getParameters()[0]->getValue(), .8f, "Relinking restores the missing plugin settings");
        const juce::File output (argv[3]); output.createDirectory();
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor()); editor->setSize (1120, 820);
        require (sattari::test::renderComponentToPng (*editor, output.getChildFile ("hosted-rack.png")), "Hosted rack UI rendered");
        auto* open = find<juce::Button> (*editor, "Open hosted plugin editor"); require (open != nullptr, "Native editor control available");
        open->onClick(); editor.reset(); require (true, "Hosted editor opens and closes with the rack");
        p.clearRack(); p.set ("workspace", 2); p.loadHostedPlugin (8, instruments[0]); p.loadHostedPlugin (0, effects[0]); p.serviceHostedChanges();
        auto mono = p.getBusesLayout(); mono.outputBuses.set (0, juce::AudioChannelSet::mono());
        if (! SATTARI_MODULAR_SYNTH) mono.inputBuses.set (0, juce::AudioChannelSet::mono());
        require (p.setBusesLayout (mono), "Mono parent layout accepted"); p.prepareToPlay (48000, 64);
        juce::AudioBuffer<float> monoBlock (1, 512);
        p.queueNote (60, .8f, true);
        for (int i = 0; i < 20; ++i) { monoBlock.clear(); p.processBlock (monoBlock, midi); }
        near (monoBlock.getSample (0, 500), .035f, "Stereo-only instrument/effect adapt to a mono parent");
        auto stereo = p.getBusesLayout(); stereo.outputBuses.set (0, juce::AudioChannelSet::stereo());
        if (! SATTARI_MODULAR_SYNTH) stereo.inputBuses.set (0, juce::AudioChannelSet::stereo());
        require (p.setBusesLayout (stereo), "Stereo parent layout restored");
        if (! SATTARI_MODULAR_SYNTH)
        {
            p.clearRack(); p.set ("workspace", 0); p.loadHostedPlugin (0, effects[0]); p.loadHostedPlugin (6, effects[0]); p.serviceHostedChanges();
            p.set ("return", 0); p.prepareToPlay (96000, 64); settle (p, true);
            b.clear(); for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < b.getNumSamples(); ++s) b.setSample (ch, s, .2f);
            p.processBlock (b, midi); near (b.getSample (0, 900), .1f, "External audio passes through hosted effect");
            const int latency = p.getLatencySamples(); p.set (P::nodeId (0, "bypass"), 1); settle (p, true);
            b.clear(); b.setSample (0, 0, .5f); p.processBlock (b, midi);
            near (b.getSample (0, latency), .5f, "Bypassed hosted effect keeps main/return timing aligned");
        }
        p.releaseResources(); recalled.releaseResources();
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << std::endl; return 1; }
}
