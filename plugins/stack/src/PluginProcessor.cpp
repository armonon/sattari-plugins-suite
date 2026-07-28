#include "PluginProcessor.h"
#include "PluginEditor.h"

// Stack's roster — the suite's INSTRUMENTS (aumu), never its effects.
//
// !! THIS ORDER IS FROZEN FROM THE FIRST RELEASE. APPEND ONLY. !!
// A layer stores an INDEX into this list (0 = Empty, N = modules[N-1]).
//
// Empty today: every instrument in the suite (Chop, Granulator, Rhythm Genie)
// still carries message-thread file loading and a lock-guarded published
// snapshot, which is a materially bigger extraction than any effect module and
// is the next piece of work. The HOST ships first and deliberately so — its AU
// type and layering contract are one-way doors, and getting those wrong after
// instruments exist would cost every saved session.
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeStackModules()
{
    return {};
}

SattariStackAudioProcessor::SattariStackAudioProcessor()
    : sattari::rack::SattariInstrumentRack (
          "Sattari Stack", "STACK",
          BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true),
          &makeStackModules, "stack")
{
    registerFactoryPresets();
}

// Presets land with the instruments — a stack preset names instruments, and there
// are none in the roster yet. The mechanism (addStackPreset) is in the base and is
// exercised by the other three racks' equivalent.
void SattariStackAudioProcessor::registerFactoryPresets()
{
}

juce::AudioProcessorEditor* SattariStackAudioProcessor::createEditor()
{
    return new SattariStackAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariStackAudioProcessor(); }
