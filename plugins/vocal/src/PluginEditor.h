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
              p, "Vocal",
              juce::String::fromUTF8 ("vocal rack \xc2\xb7 processes top to bottom"),
              "vocal")
    {
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariVocalAudioProcessorEditor)
};
