#pragma once

#include <JuceHeader.h>

struct StemRemovalAdapter
{
    virtual ~StemRemovalAdapter() = default;
    virtual juce::String getName() const = 0;
    virtual bool isAvailable() const = 0;
};

struct MicInputAdapter
{
    virtual ~MicInputAdapter() = default;
    virtual void prepare (double sampleRate, int blockSize) = 0;
    virtual bool isArmed() const = 0;
};

struct PluginHostingAdapter
{
    virtual ~PluginHostingAdapter() = default;
    virtual juce::StringArray getSupportedPluginFormats() const = 0;
    virtual bool canHostPlugins() const = 0;
};
