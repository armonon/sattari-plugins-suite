#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/modules/DoubleModule.h>
#include <sattari_audio_core/modules/FaceModule.h>
#include <sattari_audio_core/modules/GlassChoirModule.h>
#include <sattari_audio_core/modules/CleanModule.h>
#include <sattari_audio_core/modules/GateModule.h>
#include <sattari_audio_core/modules/EqModule.h>
#include <sattari_audio_core/modules/CompModule.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <sattari_audio_core/modules/SpaceModule.h>

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
    v.push_back (std::make_unique<sattari::dsp::CleanModule>());   // 5
    v.push_back (std::make_unique<sattari::dsp::GateModule>());    // 6
    v.push_back (std::make_unique<sattari::dsp::EqModule>());      // 7
    v.push_back (std::make_unique<sattari::dsp::CompModule>());    // 8
    v.push_back (std::make_unique<sattari::dsp::HeatModule>());    // 9
    v.push_back (std::make_unique<sattari::dsp::SpaceModule>());   // 10
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

    addChainPreset ("Vocal Studio",
        { "clean", "gate", "deess", "eq", "comp", "heat", "double", "space" },
        PV{ {"clean.amount", 0.25f}, {"gate.threshold", -48.0f},
            {"deess.threshold", -30.0f}, {"deess.range", 7.0f},
            {"comp.threshold", -20.0f}, {"comp.ratio", 3.0f}, {"comp.autoRelease", 1.0f},
            {"heat.drive", 3.0f}, {"heat.mix", 0.35f},
            {"double.detune", 8.0f}, {"double.timing", 14.0f}, {"double.level", 0.28f},
            {"space.revmix", 0.16f}, {"space.delaymix", 0.08f} });

    addChainPreset ("Lead Polish",
        { "deess", "eq", "comp", "heat", "double", "space" },
        PV{ {"deess.threshold", -29.0f}, {"deess.range", 8.0f},
            {"comp.threshold", -18.0f}, {"comp.ratio", 3.5f}, {"comp.attack", 12.0f},
            {"heat.drive", 4.0f}, {"heat.mix", 0.42f},
            {"double.level", 0.22f}, {"space.revmix", 0.2f} });

    addChainPreset ("Dialogue Focus",
        { "clean", "gate", "deess", "eq", "comp" },
        PV{ {"clean.amount", 0.4f}, {"gate.threshold", -46.0f}, {"gate.range", 24.0f},
            {"deess.threshold", -33.0f}, {"deess.range", 9.0f},
            {"comp.threshold", -22.0f}, {"comp.ratio", 2.5f}, {"comp.autoGain", 1.0f} });

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
