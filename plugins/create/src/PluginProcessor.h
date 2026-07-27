#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackProcessor.h>

// =============================================================================
//  Sattari Create — the creative-FX rack.
// -----------------------------------------------------------------------------
//  The time/space/character counterpart to Sattari Mix: 8 reorderable slots
//  holding the suite's creative effects (reverb, delay, shimmer, tape, riser,
//  saturation, pump) instead of its dynamics.
//
//  All the rack machinery lives in sattari::rack::SattariRackProcessor — Mix and
//  Create are the same machine with different rosters. This file is only the
//  roster and the identity.
//
//  Create's INSTRUMENTS (Granulator, Vocoder, 808) are deliberately NOT here:
//  an instrument is an `aumu` plugin and an effect rack is not, so they need a
//  separate instrument host rather than a slot in this one.
// =============================================================================
class SattariCreateAudioProcessor final : public sattari::rack::SattariRackProcessor
{
public:
    SattariCreateAudioProcessor();
    ~SattariCreateAudioProcessor() override = default;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Reverbs and delays in the roster, so tails must not be cut off.
    double getTailLengthSeconds() const override { return 6.0; }

private:
    void registerFactoryPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCreateAudioProcessor)
};
