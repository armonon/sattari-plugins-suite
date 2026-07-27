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
          &makeMixModules, "mix")
{
    registerFactoryPresets();
}

// Factory presets. Two kinds, and the second is the point of the whole rack:
//
//  * ordinary starting chains (Vocal Chain, Drum Bus, ...);
//  * COLLAPSED PLUGINS — "Bus" and "Royal Chain" were separate plugins whose only
//    real content was a FIXED order of these same processors. A fixed chain is a
//    preset, not a product, so they live here now as wiring. The difference for
//    the user is that the order is no longer welded shut.
//
// Only the parameters a preset actually cares about are set; everything else keeps
// its default, so presets stay valid as modules gain parameters.
void SattariMixAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    addChainPreset ("Vocal Chain",
        { "gate", "deess", "eq", "comp", "heat" },
        PV{ {"gate.threshold", -45}, {"gate.range", 40},
            {"deess.threshold", -30}, {"deess.range", 8},
            {"eq.hpf", 90},
            {"comp.threshold", -18}, {"comp.ratio", 3.0f}, {"comp.attack", 8}, {"comp.release", 120},
            {"heat.drive", 0.25f} });

    addChainPreset ("Drum Bus",
        { "transient", "comp", "multiband", "heat", "peak" },
        PV{ {"transient.attack", 0.35f}, {"transient.sustain", -0.15f},
            {"comp.threshold", -14}, {"comp.ratio", 4.0f}, {"comp.attack", 15}, {"comp.release", 90},
            {"heat.drive", 0.3f} });

    addChainPreset ("Master Bus",
        { "eq", "multiband", "image", "peak", "meter" },
        PV{ {"eq.hpf", 25},
            {"peak.ceiling", -1.0f} });

    addChainPreset ("Clean Up",
        { "clean", "gate", "eq", "deess" },
        PV{ {"gate.threshold", -50}, {"eq.hpf", 80} });

    addChainPreset ("Duck To Kick",
        { "sidechain", "eq", "comp" },
        PV{ {"sidechain.external", 1}, {"sidechain.depth", 8}, {"sidechain.release", 160} });

    // --- Collapsed plugins ---------------------------------------------------
    addChainPreset ("Bus (classic)",
        { "eq", "comp", "heat", "image", "peak" },
        PV{ {"eq.hpf", 30},
            {"comp.threshold", -16}, {"comp.ratio", 2.5f}, {"comp.attack", 20}, {"comp.release", 150},
            {"heat.drive", 0.2f},
            {"image.width", 1.1f},
            {"peak.ceiling", -0.3f} });

    addChainPreset ("Royal Chain (classic)",
        { "gate", "deess", "eq", "comp", "multiband", "heat", "peak" },
        PV{ {"gate.threshold", -48}, {"gate.range", 35},
            {"deess.threshold", -32}, {"deess.range", 10},
            {"eq.hpf", 95},
            {"comp.threshold", -20}, {"comp.ratio", 3.5f}, {"comp.attack", 6}, {"comp.release", 110},
            {"heat.drive", 0.3f},
            {"peak.ceiling", -0.5f} });
}

juce::AudioProcessorEditor* SattariMixAudioProcessor::createEditor()
{
    return new SattariMixAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariMixAudioProcessor(); }
