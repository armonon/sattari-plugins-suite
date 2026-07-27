#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/EchoModule.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <sattari_audio_core/modules/PulseModule.h>
#include <sattari_audio_core/modules/RiserModule.h>
#include <sattari_audio_core/modules/ShimmerModule.h>
#include <sattari_audio_core/modules/SpaceModule.h>
#include <sattari_audio_core/modules/WarbleModule.h>

// Create's roster. Heat and Pulse appear in Mix as well — a module is DSP, not a
// product, so the same implementation can sit in more than one rack.
//
// !! THIS ORDER IS FROZEN FROM THE FIRST RELEASE. APPEND ONLY. !!
// A slot stores an INDEX into this list (0 = Empty, N = modules[N-1]), so
// inserting or reordering silently rewires every saved session to a different
// processor. New modules go at the END.
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeCreateModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> v;
    v.push_back (std::make_unique<sattari::dsp::EchoModule>());      // 1
    v.push_back (std::make_unique<sattari::dsp::HeatModule>());      // 2
    v.push_back (std::make_unique<sattari::dsp::PulseModule>());     // 3
    v.push_back (std::make_unique<sattari::dsp::RiserModule>());     // 4
    v.push_back (std::make_unique<sattari::dsp::ShimmerModule>());   // 5
    v.push_back (std::make_unique<sattari::dsp::SpaceModule>());     // 6
    v.push_back (std::make_unique<sattari::dsp::WarbleModule>());    // 7
    return v;
}

SattariCreateAudioProcessor::SattariCreateAudioProcessor()
    : sattari::rack::SattariRackProcessor (
          "Sattari Create", "CREATE",
          BusesProperties()
              .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
              // Carried for parity with Mix: a creative rack wants keyed ducking too
              // (a pad pumped under a kick), and the bus has to exist from the first
              // release because adding one later changes the plugin's channel layout.
              .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput ("Output",    juce::AudioChannelSet::stereo(), true),
          &makeCreateModules)
{
}

juce::AudioProcessorEditor* SattariCreateAudioProcessor::createEditor()
{
    return new SattariCreateAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariCreateAudioProcessor(); }
