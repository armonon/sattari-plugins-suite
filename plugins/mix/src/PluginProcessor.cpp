#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/AlignModule.h>
#include <sattari_audio_core/modules/CleanModule.h>
#include <sattari_audio_core/modules/CompModule.h>
#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/modules/EqModule.h>
#include <sattari_audio_core/modules/GateModule.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <sattari_audio_core/modules/ImageModule.h>
#include <sattari_audio_core/modules/MeterModule.h>
#include <sattari_audio_core/modules/MultibandModule.h>
#include <sattari_audio_core/modules/PeakModule.h>
#include <sattari_audio_core/modules/PulseModule.h>
#include <sattari_audio_core/modules/SideChainMasterModule.h>
#include <sattari_audio_core/modules/TransientModule.h>

// The rack's module roster. Adding a module here is the ONLY step needed to make
// it available in every slot — its parameters, preparation and reset all follow.
//
// !! THIS ORDER IS FROZEN. APPEND ONLY. !!
// A slot stores an INDEX into this list (0 = Empty, N = modules[N-1]), so
// inserting or reordering a module silently rewires every saved session and
// preset to a different processor. New modules go at the END, alphabetical or
// not. The menu is cosmetic; the order is data.
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeMixModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> v;
    v.push_back (std::make_unique<sattari::dsp::CleanModule>());              // 1
    v.push_back (std::make_unique<sattari::dsp::CompModule>());               // 2
    v.push_back (std::make_unique<sattari::dsp::DeEssModule>());              // 3
    v.push_back (std::make_unique<sattari::dsp::EqModule>());                 // 4
    v.push_back (std::make_unique<sattari::dsp::GateModule>());               // 5
    v.push_back (std::make_unique<sattari::dsp::HeatModule>());               // 6
    v.push_back (std::make_unique<sattari::dsp::ImageModule>());              // 7
    v.push_back (std::make_unique<sattari::dsp::MultibandModule>());          // 8
    v.push_back (std::make_unique<sattari::dsp::MeterModule>());              // 9
    v.push_back (std::make_unique<sattari::dsp::PeakModule>());               // 10
    v.push_back (std::make_unique<sattari::dsp::PulseModule>());              // 11
    v.push_back (std::make_unique<sattari::dsp::TransientModule>());          // 12
    v.push_back (std::make_unique<sattari::dsp::SideChainMasterModule>());    // 13
    v.push_back (std::make_unique<sattari::dsp::AlignModule>());              // 14
    return v;
}

SattariMixAudioProcessor::SattariMixAudioProcessor()
    : sattari::rack::SattariRackProcessor (
          "Sattari Mix", "MIX",
          BusesProperties()
              .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
              // Optional external key. A channel strip is expected to compress a bass
              // to a kick or gate a snare to a trigger; without this bus the rack
              // structurally cannot, and two-signal modules (Align, Side Chain Master)
              // cannot exist at all.
              .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput ("Output",    juce::AudioChannelSet::stereo(), true),
          &makeMixModules)
{
}

juce::AudioProcessorEditor* SattariMixAudioProcessor::createEditor()
{
    return new SattariMixAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariMixAudioProcessor(); }
