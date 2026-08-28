#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariStackEditor.h>
#include "PluginProcessor.h"

// Stack's UI is the shared instrument-rack editor; only the identity differs.
class SattariStackAudioProcessorEditor final : public sattari::ui::SattariStackEditor
{
public:
    explicit SattariStackAudioProcessorEditor (SattariStackAudioProcessor& p)
        : sattari::ui::SattariStackEditor (
              p, "Sattari Stack", "INSTRUMENT LAYER WORKSPACE", "stack")
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariStackAudioProcessorEditor)
};
