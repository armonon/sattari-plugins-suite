#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackEditor.h>
#include "PluginProcessor.h"

// Vocal's UI is the shared rack editor; only the identity differs.
class SattariVocalAudioProcessorEditor final : public sattari::ui::SattariRackEditor
{
public:
    explicit SattariVocalAudioProcessorEditor (SattariVocalAudioProcessor& p)
        : sattari::ui::SattariRackEditor (
              p, "Sattari Vocal", "VOCAL FINISHING WORKFLOW", "vocal")
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVocalAudioProcessorEditor)
};
