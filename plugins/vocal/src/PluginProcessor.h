#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackProcessor.h>

// =============================================================================
//  Sattari Vocal — the vocal-finishing rack.
// -----------------------------------------------------------------------------
//  "Finish the vocal": 8 reorderable slots holding the suite's vocal utilities —
//  de-essing, doubling, formant/gender shaping — so a vocal chain is assembled
//  rather than fixed.
//
//  All the rack machinery lives in sattari::rack::SattariRackProcessor — Mix,
//  Create and Vocal are the same machine with different rosters. This file is
//  only the roster and the identity.
//
//  The FLAGSHIPS stay standalone and are deliberately NOT slots here: Auto Pitch
//  (ONNX + worker thread + Rubber Band + Key Map), Harmony, Maqam and VoxKey.
//  Racking a benchmarked engine risks the benchmark for no user-visible gain.
// =============================================================================
class SattariVocalAudioProcessor final : public sattari::rack::SattariRackProcessor
{
public:
    SattariVocalAudioProcessor();
    ~SattariVocalAudioProcessor() override = default;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Face carries Rubber Band latency and Double a delay line, so leave a tail.
    double getTailLengthSeconds() const override { return 0.5; }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVocalAudioProcessor)
};
