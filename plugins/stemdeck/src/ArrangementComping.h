#pragma once

#include <JuceHeader.h>

namespace stemdeck::comping
{
struct SourceRegion
{
    juce::File file;
    double sourceDuration = 0.0;
    double trimStart = 0.0;
    double trimEnd = 0.0;
    bool isValid() const noexcept
    { return file.existsAsFile() && trimEnd > trimStart; }
};

// Resolve a timeline region against an alternate take without depending on the
// arrangement component. A candidate must cover the complete requested region;
// partial coverage is rejected so a comp never introduces an accidental hole.
inline SourceRegion findCoveringSource (const juce::ValueTree& takeState,
                                        int laneIndex, double targetStart,
                                        double targetEnd)
{
    if (! takeState.hasType ("ARRANGEMENT")
        || laneIndex < 0 || targetEnd - targetStart < 0.01)
        return {};

    juce::ValueTree sourceLane;
    for (int i = 0; i < takeState.getNumChildren(); ++i)
    {
        const auto candidate = takeState.getChild (i);
        if (candidate.hasType ("LANE")
            && (int) candidate.getProperty ("index", -1) == laneIndex)
        { sourceLane = candidate; break; }
    }
    if (! sourceLane.isValid()) return {};

    for (int i = 0; i < sourceLane.getNumChildren(); ++i)
    {
        const auto source = sourceLane.getChild (i);
        if (! source.hasType ("CLIP")) continue;
        const double timelineStart = source.getProperty ("tlStart", 0.0);
        const double sourceIn = source.getProperty ("trimStart", 0.0);
        const double sourceOut = source.getProperty ("trimEnd", 0.0);
        const double timelineEnd = timelineStart
                                 + juce::jmax (0.0, sourceOut - sourceIn);
        if (timelineStart > targetStart + 0.001
            || timelineEnd < targetEnd - 0.001)
            continue;

        SourceRegion result;
        result.file = juce::File (source.getProperty ("file").toString());
        result.sourceDuration = source.getProperty ("dur", 0.0);
        result.trimStart = sourceIn + (targetStart - timelineStart);
        result.trimEnd = result.trimStart + (targetEnd - targetStart);
        if (result.isValid()) return result;
    }
    return {};
}
}
