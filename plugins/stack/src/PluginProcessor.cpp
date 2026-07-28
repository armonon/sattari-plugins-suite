#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <sattari_audio_core/modules/ChopModule.h>

// Stack's roster — the suite's INSTRUMENTS (aumu), never its effects.
//
// !! THIS ORDER IS FROZEN FROM THE FIRST RELEASE. APPEND ONLY. !!
// A layer stores an INDEX into this list (0 = Empty, N = modules[N-1]).
//
// Granulator and Rhythm Genie append here next; both are larger and carry more
// message-thread machinery than Chop did.
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeStackModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> v;
    v.push_back (std::make_unique<sattari::dsp::ChopModule>());   // 1
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
}

juce::AudioProcessorEditor* SattariStackAudioProcessor::createEditor()
{
    return new SattariStackAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariStackAudioProcessor(); }
