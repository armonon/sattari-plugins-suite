#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackEditor.h>
#include "PluginProcessor.h"

// Create's UI is the shared rack editor; only the identity differs.
class SattariCreateAudioProcessorEditor final : public sattari::ui::SattariRackEditor
{
public:
    explicit SattariCreateAudioProcessorEditor (SattariCreateAudioProcessor& p)
        : sattari::ui::SattariRackEditor (
              p, "Sattari Create", "CREATIVE EFFECTS WORKFLOW", "create")
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariCreateAudioProcessorEditor)
};
