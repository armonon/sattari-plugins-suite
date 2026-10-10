#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>

// Exercise the real processor's recall path when hosts send fractional switch values.
inline int runBooleanStateContract (juce::AudioProcessor& processor)
{
    juce::MemoryBlock original;
    processor.getStateInformation (original);
    int failures = 0, checks = 0;
    for (auto* parameter : processor.getParameters())
    {
        if (! parameter->isBoolean()) continue;
        for (const float expected : { 0.0f, 1.0f })
        {
            parameter->setValueNotifyingHost (expected);
            juce::MemoryBlock saved;
            processor.getStateInformation (saved);
            for (const float fractional : { 0.464972f, 0.535028f })
            {
                parameter->setValueNotifyingHost (fractional);
                processor.setStateInformation (saved.getData(), (int) saved.getSize());
                ++checks;
                if (saved.isEmpty() || std::abs (parameter->getValue() - expected) > 1.0e-6f)
                {
                    ++failures;
                    std::fprintf (stderr, "FAIL: %s boolean recall: expected %.0f, got %.6f after %.6f automation\n",
                                  parameter->getName (128).toRawUTF8(), expected, parameter->getValue(), fractional);
                }
            }
        }
    }
    processor.setStateInformation (original.getData(), (int) original.getSize());
    std::printf ("Boolean state contract: %d/%d checks passed\n", checks - failures, checks);
    return failures;
}
