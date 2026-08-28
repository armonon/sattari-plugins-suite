#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/ChopModule.h>
#include <sattari_audio_core/modules/GranulatorModule.h>
#include <sattari_audio_core/modules/RhythmGenieModule.h>

// Stack's roster — the suite's INSTRUMENTS (aumu), never its effects.
//
// !! THIS ORDER IS FROZEN FROM THE FIRST RELEASE. APPEND ONLY. !!
// A layer stores an INDEX into this list (0 = Empty, N = modules[N-1]).
//
// Granulator appends here next (1441 lines, the biggest).
//
// !! THIS ORDER IS FROZEN. APPEND ONLY. !!
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeStackModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> v;
    v.push_back (std::make_unique<sattari::dsp::ChopModule>());          // 1
    v.push_back (std::make_unique<sattari::dsp::RhythmGenieModule>());   // 2
    v.push_back (std::make_unique<sattari::dsp::GranulatorModule>());    // 3
    return v;
}

SattariStackAudioProcessor::SattariStackAudioProcessor()
    : sattari::rack::SattariInstrumentRack (
          "Sattari Stack", "STACK",
          BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true),
          &makeStackModules, "stack")
{
    registerFactoryPresets();
}

// Factory presets. A stack preset is a set of LAYERS, and each layer is a
// placement — instrument, level, pan, key range, transpose — because that is what
// distinguishes a split from an octave layer from a plain unison double.
//
// A layer holding Chop plays nothing until a sample is loaded into it; these set
// up the ARRANGEMENT, which is the part a preset can meaningfully carry.
void SattariStackAudioProcessor::registerFactoryPresets()
{
    // Single layer, full range — the plain starting point.
    addStackPreset ("Single", { { "chop", -6.0f, 0.0f, 0, 127, 0 } });

    // SPLIT: the same instrument twice, either side of C3 (MIDI 60), the lower
    // half dropped an octave. This is the shape a stack exists for.
    addStackPreset ("Split at C3",
        { { "chop", -6.0f, -0.2f,  0,  59, -12 },
          { "chop", -6.0f,  0.2f, 60, 127,   0 } });

    // OCTAVE LAYER: one instrument doubled an octave up, quieter and panned apart
    // so the double reads as width rather than as a second note.
    addStackPreset ("Octave Layer",
        { { "chop",  -6.0f, -0.3f, 0, 127,  0 },
          { "chop", -12.0f,  0.3f, 0, 127, 12 } });

    // Kit + sampler together — the case layering exists for. The drum lanes sit in
    // their own key range so the two never fight for the same notes.
    addStackPreset ("Kit + Sampler",
        { { "rhythmgenie", -6.0f, 0.0f,  0,  47, 0 },
          { "chop",        -8.0f, 0.0f, 48, 127, 0 } });

    // Three instruments at once, which is what four layers are for: drums low, a
    // sampler in the middle, granular texture on top an octave up and panned wide.
    addStackPreset ("Full Stack",
        { { "rhythmgenie", -7.0f,  0.0f,  0,  47,  0 },
          { "chop",        -9.0f, -0.2f, 48,  83,  0 },
          { "granulator", -10.0f,  0.3f, 60, 127, 12 } });
}

juce::AudioProcessorEditor* SattariStackAudioProcessor::createEditor()
{
    return new SattariStackAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariStackAudioProcessor(); }
