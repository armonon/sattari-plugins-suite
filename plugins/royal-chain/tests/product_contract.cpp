#include "RoyalChainProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>
#include <iostream>
#include <stdexcept>

static void check (bool ok, const char* message)
{ if (! ok) throw std::runtime_error (message); std::cout << "PASS " << message << '\n'; }
static void set (RoyalChainAudioProcessor& p, const juce::String& id, float value)
{ auto* parameter = p.parameters().getParameter (id); parameter->setValueNotifyingHost (parameter->convertTo0to1 (value)); }
static juce::ComboBox* presetMenu (juce::Component& c)
{
    if (auto* box = dynamic_cast<juce::ComboBox*> (&c)) if (box->getTitle() == "Factory presets") return box;
    for (auto* child : c.getChildren()) if (auto* box = presetMenu (*child)) return box;
    return nullptr;
}
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        RoyalChainAudioProcessor p;
        check (p.getNumPrograms() == 18, "Canonical preset bank includes original host and editor presets");
        check (p.getProgramName (0) == "Modern Rap" && p.getProgramName (7) == "Demo Rescue", "Original host program indices preserved");
        const char* originalIds[] { "style", "input", "output", "clean", "tune", "tuneKey", "syncFromAutoPitch", "tuneScale", "control", "tone", "air", "smooth", "glow", "width", "space", "royal" };
        for (int i = 0; i < 16; ++i)
            check (dynamic_cast<juce::AudioProcessorParameterWithID*> (p.getParameters()[i])->paramID == originalIds[i], "Original parameter index preserved");
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* menu = presetMenu (*editor); check (menu && menu->getNumItems() == p.getNumPrograms(), "Editor and host preset lists agree");
        juce::MemoryBlock baseline; p.getStateInformation (baseline);
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setStateInformation (baseline.getData(), (int) baseline.getSize());
            p.setCurrentProgram (i); std::vector<float> expected;
            for (auto* parameter : p.getParameters()) expected.push_back (parameter->getValue());
            p.setStateInformation (baseline.getData(), (int) baseline.getSize());
            menu->setSelectedId (i + 1, juce::dontSendNotification); menu->onChange();
            bool identical = true;
            for (int j = 0; j < p.getParameters().size(); ++j)
                identical = identical && std::abs (p.getParameters()[j]->getValue() - expected[(size_t) j]) < 1.e-6f;
            check (identical, "Host and editor load identical preset values from the same initial state");
        }
        if (argc > 1)
        {
            const juce::File out (argv[1]); out.createDirectory();
            for (const auto size : { juce::Point<int> (935, 680), juce::Point<int> (1100, 800), juce::Point<int> (1540, 1120) })
            { editor->setSize (size.x, size.y); check (sattari::test::renderComponentToPng (*editor, out.getChildFile ("royal-" + juce::String (size.x) + ".png")), "Editor renders"); }
        }
        for (const double sr : { 44100.0, 48000.0, 96000.0 })
        {
            for (int i = 0; i < p.stageCount; ++i) set (p, p.stageBypassId (i), 1);
            set (p, "auditionOriginal", 0); set (p, "levelMatch", 0);
            p.prepareToPlay (sr, 64);
            juce::AudioBuffer<float> audio (2, 1024); audio.clear(); audio.setSample (0, 0, .2f); audio.setSample (1, 0, .2f);
            juce::MidiBuffer midi; p.processBlock (audio, midi);
            bool null = true;
            for (int s = 0; s < audio.getNumSamples(); ++s)
                null = null && std::abs (audio.getSample (0, s) - (s == p.getLatencySamples() ? .2f : 0)) < 1.e-6f;
            check (null, "All-stage bypass is latency-aligned dry, including oversized blocks");
            for (int i = 0; i < p.stageCount; ++i) set (p, p.stageBypassId (i), 0);
            set (p, "auditionOriginal", 1); p.prepareToPlay (sr, 64);
            audio.clear(); audio.setSample (0, 0, .2f); p.processBlock (audio, midi);
            check (std::abs (audio.getSample (0, p.getLatencySamples()) - .2f) < 1.e-6f, "Original audition preserves reported latency");
            p.releaseResources();
        }
        auto legacy = p.parameters().copyState();
        for (int i = legacy.getNumChildren(); --i >= 0;)
        { const auto id = legacy.getChild (i)["id"].toString(); if (id.startsWith ("stageBypass") || id == "auditionOriginal" || id == "levelMatch") legacy.removeChild (i, nullptr); }
        juce::MemoryBlock state; juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), state);
        set (p, "levelMatch", 1); p.setStateInformation (state.getData(), (int) state.getSize());
        check (p.parameters().getRawParameterValue ("levelMatch")->load() < .5f, "Old states restore new comparison controls to off");
        for (int i = 0; i < p.stageCount; ++i) check (p.parameters().getRawParameterValue (p.stageBypassId (i))->load() < .5f, "Old states keep processing stages enabled");
        for (const float value : { .22f, .81f })
        {
            for (auto* parameter : p.getParameters()) if (parameter->isBoolean()) parameter->setValueNotifyingHost (value);
            p.getStateInformation (state);
            for (auto* parameter : p.getParameters()) if (parameter->isBoolean()) parameter->setValueNotifyingHost (1 - value);
            p.setStateInformation (state.getData(), (int) state.getSize());
            bool canonical = true;
            for (auto* parameter : p.getParameters()) if (parameter->isBoolean())
                canonical = canonical && std::abs (parameter->getValue() - (value >= .5f ? 1.0f : 0.0f)) < .00001f;
            check (canonical, "Fractional boolean automation restores canonical state");
        }
        const auto comparisonRms = [&] (float outputDb, bool match)
        {
            for (int i = 0; i < p.stageCount; ++i) set (p, p.stageBypassId (i), i == 8 ? 0.0f : 1.0f);
            set (p, "output", outputDb); set (p, "royal", 0); set (p, "auditionOriginal", 0); set (p, "levelMatch", match ? 1.0f : 0.0f);
            p.prepareToPlay (48000, 256);
            juce::AudioBuffer<float> audio (2, 256); juce::MidiBuffer midi;
            for (int block = 0; block < 500; ++block)
            {
                for (int ch = 0; ch < 2; ++ch) for (int s = 0; s < 256; ++s)
                    audio.setSample (ch, s, .05f * std::sin (juce::MathConstants<float>::twoPi * (float) (block * 256 + s) / 48));
                p.processBlock (audio, midi);
            }
            const float rms = audio.getRMSLevel (0, 0, 256); p.releaseResources(); return rms;
        };
        const auto boosted = comparisonRms (6, false), matched = comparisonRms (6, true);
        check (boosted > matched * 1.8f && std::abs (matched - .05f / std::sqrt (2.0f)) < .002f, "Level matching attenuates a louder processed signal toward original RMS");
        const auto quiet = comparisonRms (-6, false), quietMatched = comparisonRms (-6, true);
        check (std::abs (quiet - quietMatched) < .00001f, "Level matching never boosts a quieter processed signal");
        std::cout << "Royal Chain product contract passed\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
