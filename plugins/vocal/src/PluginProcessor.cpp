#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/modules/DoubleModule.h>
#include <sattari_audio_core/modules/FaceModule.h>

// Vocal's roster. De-Ess is also in Mix — a module is DSP, not a product.
//
// !! THIS ORDER IS FROZEN FROM THE FIRST RELEASE. APPEND ONLY. !!
// A slot stores an INDEX into this list (0 = Empty, N = modules[N-1]), so
// inserting or reordering silently rewires every saved session to a different
// processor. New modules (Glass Choir next) go at the END.
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeVocalModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> v;
    v.push_back (std::make_unique<sattari::dsp::DeEssModule>());   // 1
    v.push_back (std::make_unique<sattari::dsp::DoubleModule>());  // 2
    v.push_back (std::make_unique<sattari::dsp::FaceModule>());    // 3
    return v;
}

SattariVocalAudioProcessor::SattariVocalAudioProcessor()
    : sattari::rack::SattariRackProcessor (
          "Sattari Vocal", "VOCAL",
          BusesProperties()
              .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
              // Carried for parity with the other racks: a vocal chain wants keyed
              // ducking too (a vocal ducked under a lead), and the bus must exist
              // from the first release because adding one later changes the
              // plugin's channel layout.
              .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput ("Output",    juce::AudioChannelSet::stereo(), true),
          &makeVocalModules)
{
}

juce::AudioProcessorEditor* SattariVocalAudioProcessor::createEditor()
{
    return new SattariVocalAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariVocalAudioProcessor(); }
