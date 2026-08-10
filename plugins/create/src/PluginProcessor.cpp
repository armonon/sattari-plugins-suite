#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/EchoModule.h>
#include <sattari_audio_core/modules/EightOhEightModule.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <sattari_audio_core/modules/PulseModule.h>
#include <sattari_audio_core/modules/RiserModule.h>
#include <sattari_audio_core/modules/ShimmerModule.h>
#include <sattari_audio_core/modules/SpaceModule.h>
#include <sattari_audio_core/modules/VocoderModule.h>
#include <sattari_audio_core/modules/WarbleModule.h>
#include <sattari_audio_core/modules/WarpModule.h>

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
    v.push_back (std::make_unique<sattari::dsp::WarpModule>());      // 8
    v.push_back (std::make_unique<sattari::dsp::VocoderModule>());   // 9
    v.push_back (std::make_unique<sattari::dsp::EightOhEightModule>());  // 10
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
          &makeCreateModules, "create")
{
    registerFactoryPresets();
}

// Factory presets: starting chains for the creative rack. Only the parameters a
// preset cares about are set; everything else keeps its default.
void SattariCreateAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    addChainPreset ("Ambient Wash",
        { "space", "shimmer", "echo" },
        PV{ {"space.size", 0.9f}, {"space.revmix", 0.45f},
            {"shimmer.shimmer", 0.5f}, {"shimmer.mix", 0.35f},
            {"echo.feedback", 0.55f}, {"echo.mix", 0.3f} });

    addChainPreset ("Tape Slap",
        { "warble", "echo" },
        PV{ {"warble.age", 0.5f},
            {"echo.sync", 0}, {"echo.timems", 120}, {"echo.feedback", 0.15f}, {"echo.mix", 0.28f} });

    addChainPreset ("EDM Pump",
        { "pulse", "space" },
        PV{ {"pulse.depth", 0.85f}, {"pulse.rate", 2},
            {"space.revmix", 0.25f} });

    // The suite-native creative workflow: Pulse and Echo follow the approved
    // Compass tempo, while Shimmer/Space provide a restrained musical lift.
    addChainPreset ("Compass Motion",
        { "pulse", "echo", "shimmer", "space" },
        PV{ {"pulse.shape", 0.0f}, {"pulse.rate", 4.0f},
            {"pulse.depth", 0.55f}, {"pulse.smooth", 0.4f}, {"pulse.mix", 0.8f},
            {"echo.sync", 1.0f}, {"echo.div", 4.0f}, {"echo.feedback", 0.32f},
            {"echo.duck", 0.45f}, {"echo.mix", 0.22f},
            {"shimmer.shimmer", 0.3f}, {"shimmer.mix", 0.2f},
            {"space.size", 0.72f}, {"space.revmix", 0.24f} });

    addChainPreset ("Robot Vocal",
        { "warp", "echo" },
        PV{ {"warp.dose", 0.7f}, {"warp.robotAmt", 0.8f},
            {"echo.mix", 0.25f} });

    addChainPreset ("Vocoded Pad",
        { "vocoder", "space", "shimmer" },
        PV{ {"vocoder.bands", 24}, {"vocoder.voicing", 3},
            {"space.size", 0.85f}, {"space.revmix", 0.4f},
            {"shimmer.mix", 0.3f} });

    addChainPreset ("Transition Build",
        { "riser", "space" },
        PV{ {"riser.length", 1}, {"riser.mix", 0.85f},
            {"space.revmix", 0.3f} });

    addChainPreset ("808 Sub Layer",
        { "eightoheight", "heat" },
        PV{ {"eightoheight.blend", 0.6f}, {"eightoheight.decay", 0.5f},
            {"heat.drive", 0.2f} });

    addChainPreset ("Lo-Fi Grit",
        { "warble", "heat", "echo" },
        PV{ {"warble.age", 0.7f}, {"warble.crackle", 0.5f},
            {"heat.drive", 0.5f},
            {"echo.mix", 0.2f}, {"echo.tone", -0.4f} });
}

juce::AudioProcessorEditor* SattariCreateAudioProcessor::createEditor()
{
    return new SattariCreateAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariCreateAudioProcessor(); }
