#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackProcessor.h>

// =============================================================================
//  Sattari Mix — the mixing/dynamics rack.
// -----------------------------------------------------------------------------
//  A channel strip you assemble: 8 reorderable slots, each picking one of the
//  suite's mix modules (or Empty) with its own bypass.
//
//  All the rack machinery lives in sattari::rack::SattariRackProcessor — Mix and
//  Create are the same machine with different rosters. This file is only the
//  roster and the identity.
//
//  The modules are the SAME implementations the standalone plugins wrap, so Mix
//  and the individual plugins can never drift apart.
// =============================================================================
class SattariMixAudioProcessor final : public sattari::rack::SattariRackProcessor
{
public:
    SattariMixAudioProcessor();
    ~SattariMixAudioProcessor() override = default;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

private:
    void registerFactoryPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMixAudioProcessor)
};
