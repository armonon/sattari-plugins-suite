#include "PluginProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>
#include <sattari_audio_core/SattariLogo.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using P = SattariModularProcessor;
namespace
{
struct StoppedTransport final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    { PositionInfo info; info.setBpm (120); info.setPpqPosition (0); info.setIsPlaying (false); return info; }
};
template <typename T> T* findControl (juce::Component& parent, const juce::String& name)
{
    if (auto* control = dynamic_cast<T*> (&parent)) if (control->getTitle() == name) return control;
    for (auto* child : parent.getChildren()) if (auto* result = findControl<T> (*child, name)) return result;
    return nullptr;
}
void inspectLayout (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (! child->isVisible()) continue;
        if ((dynamic_cast<juce::Slider*> (child) || dynamic_cast<juce::Button*> (child) || dynamic_cast<juce::ComboBox*> (child))
            && child->getTitle().isNotEmpty() && (! parent.getLocalBounds().contains (child->getBounds()) || child->getBounds().isEmpty()))
            throw std::runtime_error (("Control does not fit: " + child->getTitle()).toStdString());
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
            if (std::abs (slider->getValue() - slider->getValueFromText (slider->getTextFromValue (slider->getValue()))) > juce::jmax (0.011, slider->getInterval()))
                throw std::runtime_error (("Display value does not round-trip: " + child->getTitle()).toStdString());
        inspectLayout (*child);
    }
}
void check (bool result, const char* message)
{ if (! result) throw std::runtime_error (message); std::cout << "PASS " << message << std::endl; }
void near (float a, float b, const char* message) { check (std::abs (a - b) < 0.0001f, message); }
void process (P& p, juce::AudioBuffer<float>& b, int blocks, bool sine = false, bool bypass = false)
{
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
    {
        b.clear();
        if (sine) for (int ch = 0; ch < b.getNumChannels(); ++ch) for (int s = 0; s < b.getNumSamples(); ++s)
            b.setSample (ch, s, 0.2f * std::sin ((float) (block * b.getNumSamples() + s) * 0.0575958653f));
        if (bypass) p.processBlockBypassed (b, midi); else p.processBlock (b, midi);
        for (int ch = 0; ch < b.getNumChannels(); ++ch) for (int s = 0; s < b.getNumSamples(); ++s)
            if (! std::isfinite (b.getSample (ch, s))) throw std::runtime_error ("Non-finite audio");
    }
}
void writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    file.deleteFile(); juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (audio.getNumChannels()).withBitsPerSample (24));
    if (! writer) throw std::runtime_error ("WAV creation failed");
    if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) throw std::runtime_error ("WAV write failed");
}
}

#include "ReleaseChecks.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        const auto output = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("modular-verification");
        output.createDirectory();
        P p;
        for (int type = 1; type < P::typeCount; ++type) check (p.info (type, 0).valid, ("Metadata " + P::typeName (type)).toRawUTF8());
        check (p.info (P::comp, 6).text (1) == "100.0%", "Compressor mix displays the correct percentage");
        check (p.info (P::heat, 2).text (1) == "100.0%", "Normalized mix displays the correct percentage");
        p.clearRack(); p.set ("workspace", 0); p.prepareToPlay (48000, 256);
        juce::AudioBuffer<float> block (2, 256); juce::MidiBuffer midi;
        process (p, block, 16);
        check (p.getLatencySamples() >= 0 && p.getLatencySamples() < 256, "Bounded fixed host latency");
        if (! SATTARI_MODULAR_SYNTH)
        {
            p.reset(); block.clear(); block.setSample (0, 0, 0.5f); block.setSample (1, 0, 0.5f); p.processBlock (block, midi);
            near (block.getSample (0, p.getLatencySamples()), 0.5f, "Empty rack preserves delayed dry input");
            float total = 0; for (int s = 0; s < 256; ++s) total += std::abs (block.getSample (0, s));
            near (total, 0.5f, "Empty parallel lane does not double dry signal");
        }
        p.selectModule (0, P::heat); p.set (P::controlId (0, 0), 0.8f);
        check (p.duplicateModule (0), "Duplicate effect inserted");
        p.set (P::controlId (1, 0), 0.2f);
        near (p.value (P::controlId (0, 0)), 0.8f, "Duplicate modules have independent parameters");
        p.set (P::macroId (0, "target1"), 1); p.set (P::macroId (0, "low1"), 0.1f); p.set (P::macroId (0, "high1"), 0.9f); p.set (P::macroId (0), 0.5f);
        process (p, block, 20, true);
        near (p.effectiveValue (0, 0), 0.5f, "Macro reaches audio-thread effective control");
        near (p.value (P::controlId (0, 0)), 0.8f, "Macro preserves base automation value");
        p.moveModule (0, 1);
        check ((int) p.value (P::macroId (0, "target1")) == 9, "Reorder retains macro assignment");
        near (p.value (P::controlId (1, 0)), 0.8f, "Reorder retains module values");
        p.undo(); near (p.value (P::controlId (0, 0)), 0.8f, "Undo restores reorder");
        p.redo(); near (p.value (P::controlId (1, 0)), 0.8f, "Redo restores reorder");
        p.loadFactory (0); p.set ("output", -3); p.selectScene (1); p.set ("output", -12); p.selectScene (0);
        near (p.value ("output"), -3, "Scene recall");
        p.undo(); check (p.selectedScene() == 1, "Undo restores scene selection"); near (p.value ("output"), -12, "Undo restores selected scene audio state");
        p.redo(); check (p.selectedScene() == 0, "Redo restores scene selection");
        p.copyAB(); p.selectAB (1); near (p.value ("output"), -3, "A/B copy");
        p.set ("output", -9); p.selectAB (0); near (p.value ("output"), -3, "A/B recall");
        juce::MemoryBlock state; p.getStateInformation (state);
        p.clearRack(); p.setStateInformation (state.getData(), (int) state.getSize());
        check (p.nodeType (0) == P::clean && p.nodeType (7) == P::space, "Host state restores rack");
        near (p.value ("output"), -3, "Host state restores parameters");
        p.selectScene (1); near (p.value ("output"), -12, "Host state retains inactive scenes");
        p.setStateInformation ("garbage", 7); near (p.value ("output"), -12, "Invalid state is ignored");
        check (p.savePreset (output.getChildFile ("roundtrip.sattarimodular")).wasOk(), "Preset writes to disk");
        p.clearRack(); check (p.loadPreset (output.getChildFile ("roundtrip.sattarimodular")).wasOk(), "Preset reads from disk");
        check (p.nodeType (0) == P::clean, "Disk preset restores rack");
        auto malformed = juce::ValueTree::readFromData (state.getData(), state.getSize());
        malformed.getChildWithName ("RACK").getChildWithName ("MODULAR_VALUES").getChild (0).setProperty ("value", std::numeric_limits<double>::quiet_NaN(), nullptr);
        juce::MemoryOutputStream invalid; malformed.writeToStream (invalid);
        auto invalidFile = output.getChildFile ("invalid.sattarimodular"); invalidFile.replaceWithData (invalid.getData(), invalid.getDataSize());
        check (p.loadPreset (invalidFile).failed(), "Malformed parameter state is rejected");
        check (p.nodeType (0) == P::clean, "Rejected preset leaves current rack intact"); invalidFile.deleteFile();

        if (! SATTARI_MODULAR_SYNTH)
        {
            p.clearRack(); p.set ("workspace", 0); p.selectModule (0, P::eq);
            auto gain = p.info (P::eq, 1); p.set (P::controlId (0, 1), gain.range.convertTo0to1 (12));
            process (p, block, 120, true); const auto boosted = block.getRMSLevel (0, 0, 256);
            p.set (P::controlId (0, 1), gain.range.convertTo0to1 (-12)); process (p, block, 120, true);
            check (boosted > block.getRMSLevel (0, 0, 256) * 2, "EQ control audibly changes real DSP");
            p.clearRack(); p.selectModule (0, P::heat); p.set (P::controlId (0, 0), 1);
            process (p, block, 40, true); const float driven = block.getRMSLevel (0, 0, 256);
            p.set (P::nodeId (0, "bypass"), 1); process (p, block, 40, true);
            check (std::abs (driven - block.getRMSLevel (0, 0, 256)) > 0.005f, "Heat produces an audible processing change");
            p.set ("output", -24); process (p, block, 40, true, true);
            check (block.getRMSLevel (0, 0, 256) > 0.1f, "Host bypass ignores output attenuation");
        }

        p.clearRack(); p.set ("workspace", 2); p.selectModule (8, P::granulator); p.set ("output", -6);
        p.reset(); block.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.75f), 0); p.processBlock (block, midi);
        float energy = 0;
        for (int i = 0; i < 80; ++i) { process (p, block, 1); energy += block.getRMSLevel (0, 0, 256); }
        check (energy > 0.01f, "Granulator generates audio from host MIDI");
        p.queueNote (60, 0, false); process (p, block, 400);
        check (block.getMagnitude (0, 256) < 0.0001f, "MIDI note-off releases instrument voice");
        for (int i = 0; i < 300; ++i) p.queueNote (60, 0.5f, true);
        p.queueNote (60, 0, false); process (p, block, 400);
        check (block.getMagnitude (0, 256) < 0.0001f, "Keyboard queue overflow cannot leave stuck notes");
        StoppedTransport stopped; p.setPlayHead (&stopped);
        p.selectModule (8, P::rhythm); p.reset(); process (p, block, 8);
        check (block.getMagnitude (0, 256) < 0.0001f, "Stopped drum sequencer stays silent without notes");
        p.queueNote (36, 0.8f, true);
        process (p, block, 1); energy = block.getRMSLevel (0, 0, 256);
        for (int i = 0; i < 20; ++i) { process (p, block, 1); energy += block.getRMSLevel (0, 0, 256); }
        check (energy > 0.01f, "Rhythm Genie generates audio from keyboard MIDI");
        p.setPlayHead (nullptr);
        juce::AudioBuffer<float> oversized (2, 1024); process (p, oversized, 4, true);
        check (true, "Oversized host buffers remain finite");

        juce::AudioBuffer<float> sample (1, 4800);
        for (int s = 0; s < 4800; ++s) sample.setSample (0, s, 0.3f * std::sin ((float) s * 0.04f));
        const auto sampleFile = output.getChildFile ("test-source.wav"); writeWav (sampleFile, sample);
        check (p.loadSample (0, sampleFile).wasOk(), "Source sample import");
        p.getStateInformation (state); sampleFile.deleteFile(); p.setStateInformation (state.getData(), (int) state.getSize());
        check (p.sampleName (0) == "test-source.wav", "Source audio is embedded in state");
        releaseChecks (p, output);
        p.releaseResources(); p.prepareToPlay (96000, 128); process (p, block, 3);
        p.releaseResources(); auto layout = p.getBusesLayout(); layout.outputBuses.set (0, juce::AudioChannelSet::mono());
        if (! SATTARI_MODULAR_SYNTH) layout.inputBuses.set (0, juce::AudioChannelSet::mono());
        check (p.setBusesLayout (layout), "Mono bus accepted"); p.prepareToPlay (44100, 256);
        juce::AudioBuffer<float> mono (1, 256); process (p, mono, 5);
        p.releaseResources(); layout.outputBuses.set (0, juce::AudioChannelSet::stereo());
        if (! SATTARI_MODULAR_SYNTH) layout.inputBuses.set (0, juce::AudioChannelSet::stereo());
        p.setBusesLayout (layout); p.prepareToPlay (48000, 256); p.loadFactory (SATTARI_MODULAR_SYNTH ? 2 : 0);
        process (p, block, 16);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr, "Native editor opens");
        check (sattari::branding::logo().isValid(), "Sattari logo asset is embedded");
        auto* outputControl = findControl<juce::Slider> (*editor, "OUTPUT"); check (outputControl != nullptr, "Output knob exists");
        outputControl->setValue (-8.5, juce::sendNotificationSync); near (p.value ("output"), -8.5f, "Native knob changes host parameter");
        auto* macroControl = findControl<juce::Slider> (*editor, "Warmth"); check (macroControl != nullptr, "Macro knob exists");
        macroControl->setValue (0.71, juce::sendNotificationSync); near (p.value (P::macroId (0)), 0.71f, "Native macro changes host parameter");
        auto* power = findControl<juce::Button> (*editor, "Master power"); check (power != nullptr, "Master power exists");
        power->onClick(); near (p.value ("bypass"), 1.0f, "Native power controls engine bypass"); power->onClick();
        for (const auto size : { juce::Point<int> (1120, 820), juce::Point<int> (1440, 1000), juce::Point<int> (1800, 1200) })
        {
            editor->setSize (size.x, size.y);
            inspectLayout (*editor);
            check (sattari::test::renderComponentToPng (*editor, output.getChildFile ("modular-" + juce::String (size.x) + ".png")), "Native editor renders");
            auto screenshot = editor->createComponentSnapshot (editor->getLocalBounds()); int bright = 0;
            for (int y = 22; y < 53; ++y) for (int x = 24; x < 150; ++x) if (screenshot.getPixelAt (x, y).getBrightness() > 0.6f) ++bright;
            check (bright > 40, "Sattari logo is visibly rendered");
        }
        editor->setSize (1440, 1000);
        auto* mapButton = findControl<juce::Button> (*editor, "Assign Warmth"); check (mapButton != nullptr, "Macro assignment button exists");
        mapButton->onClick();
        auto* map = findControl<juce::ComboBox> (*editor, "Macro target 1"); check (map != nullptr, "Native macro assignment popup opens");
        map->setSelectedId (2, juce::sendNotificationSync);
        check ((int) p.value (P::macroId (0, "target1")) == 1, "Native mapping menu changes the actual target");
        check (sattari::test::renderComponentToPng (*editor, output.getChildFile ("modular-mapping.png")), "Macro assignment editor renders");
        editor.reset();
        check (true, "Editor closes safely with its mapping popup open");
        p.loadFactory (2); p.reset();
        juce::AudioBuffer<float> render (2, 48000 * 5); render.clear();
        for (int offset = 0; offset < render.getNumSamples(); offset += 256)
        {
            const int count = juce::jmin (256, render.getNumSamples() - offset);
            juce::AudioBuffer<float> chunk (render.getArrayOfWritePointers(), 2, offset, count); midi.clear();
            for (int n = 0; n < 4; ++n)
            {
                const int on = n * 36000, off = on + 24000;
                if (on >= offset && on < offset + count) midi.addEvent (juce::MidiMessage::noteOn (1, 60 + n * 3, 0.7f), on - offset);
                if (off >= offset && off < offset + count) midi.addEvent (juce::MidiMessage::noteOff (1, 60 + n * 3), off - offset);
            }
            p.processBlock (chunk, midi);
        }
        check (render.getRMSLevel (0, 0, render.getNumSamples()) > 0.001f && render.getMagnitude (0, render.getNumSamples()) < 1.0f, "Audio demo has real unclipped output");
        writeWav (output.getChildFile ("Sattari-Modular-native-demo.wav"), render);
        std::cout << "All Modular " << (SATTARI_MODULAR_SYNTH ? "Instrument" : "FX") << " contracts passed." << std::endl;
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
}
