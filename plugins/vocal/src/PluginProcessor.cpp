#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/modules/DoubleModule.h>
#include <sattari_audio_core/modules/FaceModule.h>
#include <sattari_audio_core/modules/GlassChoirModule.h>

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
    v.push_back (std::make_unique<sattari::dsp::GlassChoirModule>());   // 4
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
          &makeVocalModules, "vocal")
{
    registerFactoryPresets();
}

// Factory presets. "Vocal Master (classic)" is a COLLAPSED PLUGIN: Sattari Vocal
// Master was a fixed de-ess -> shape -> double chain, and a fixed chain is a
// preset, not a product. Here the order is no longer welded shut.
void SattariVocalAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    addChainPreset ("Natural Double",
        { "deess", "double" },
        PV{ {"deess.threshold", -30},
            {"double.detune", 12}, {"double.timing", 18}, {"double.level", 0.6f} });

    addChainPreset ("Wide Stack",
        { "deess", "double", "face" },
        PV{ {"double.detune", 16}, {"double.timing", 24}, {"double.width", 1.0f}, {"double.level", 0.75f},
            {"face.formant", 1.5f}, {"face.mix", 0.4f} });

    addChainPreset ("Gender Shift",
        { "face" },
        PV{ {"face.formant", 5}, {"face.mix", 1.0f} });

    addChainPreset ("De-Ess Only",
        { "deess" },
        PV{ {"deess.threshold", -32}, {"deess.range", 10} });

    addChainPreset ("Ensemble",
        { "deess", "glasschoir" },
        PV{ {"glasschoir.character", 0.35f}, {"glasschoir.choirLevel", -2} });

    // --- Collapsed plugin ----------------------------------------------------
    addChainPreset ("Vocal Master (classic)",
        { "deess", "face", "double" },
        PV{ {"deess.threshold", -28}, {"deess.range", 8},
            {"face.formant", 0.0f}, {"face.mix", 0.0f},
            {"double.detune", 8}, {"double.timing", 14}, {"double.level", 0.35f} });
}

juce::AudioProcessorEditor* SattariVocalAudioProcessor::createEditor()
{
    return new SattariVocalAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariVocalAudioProcessor(); }
