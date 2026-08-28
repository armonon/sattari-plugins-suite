#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariInstrumentRack.h>

// =============================================================================
//  Sattari Stack — the instrument host.
// -----------------------------------------------------------------------------
//  Four LAYERS, each holding one of the suite's instruments. Every layer receives
//  the same MIDI and renders in parallel; the results are summed. That is the
//  whole difference from Mix / Create / Vocal, which chain their slots in series.
//
//  Because layers are parallel rather than serial, each one carries its own
//  level, pan, key range and transpose — so the same rack builds a keyboard SPLIT
//  (bass below C3, lead above) or an OCTAVE LAYER, not just a unison pile.
//
//  See sattari::rack::SattariInstrumentRack for the layering, the per-layer MIDI
//  filtering, and why reported latency is the MAX of the active layers with the
//  faster ones delay-aligned.
// =============================================================================
class SattariStackAudioProcessor final : public sattari::rack::SattariInstrumentRack
{
public:
    SattariStackAudioProcessor();
    ~SattariStackAudioProcessor() override = default;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

private:
    void registerFactoryPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariStackAudioProcessor)
};
