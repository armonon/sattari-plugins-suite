#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariRackEditor.h>
#include "PluginProcessor.h"

// Mix's UI is the shared rack editor; only the identity differs.
class SattariMixAudioProcessorEditor final : public sattari::ui::SattariRackEditor
{
public:
    explicit SattariMixAudioProcessorEditor (SattariMixAudioProcessor& p)
        : sattari::ui::SattariRackEditor (
              p, "Mix",
              juce::String::fromUTF8 ("module rack \xc2\xb7 processes top to bottom"),
              "mix")
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariMixAudioProcessorEditor)
};
