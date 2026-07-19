#pragma once

// =============================================================================
//  AnalysisCache — persistent SQLite cache of TrackAnalysis, keyed by a content
//  hash of the audio file.
// -----------------------------------------------------------------------------
//  Loading a deck currently runs three full-track analyses (DSP TrackAnalyzer +
//  neural beat sweep + neural key), ~10 s for a 5-minute track. In a studio
//  workflow most loads are RE-loads, so a cache hit turns that into a
//  sub-millisecond DB read. This is build-order item #1: the biggest instant-load
//  win for the least work, and fully reversible (delete the DB to reset).
//
//  Thread-safe: the audio loader runs up to 4 concurrent jobs; every DB access is
//  serialised by a lock and the connection opens in full-mutex mode.
// =============================================================================

#include <JuceHeader.h>
#include <memory>
#include "TrackAnalysis.h"

class AnalysisCache
{
public:
    AnalysisCache();                                   // opens the default DB
    explicit AnalysisCache (const juce::File& dbFile); // for tests
    ~AnalysisCache();

    bool isOpen() const noexcept;
    static juce::File defaultDbFile();

    // Path-independent content hash: file size + sampled byte windows (start /
    // middle / end). Fast even for large files; robust to moves/renames.
    static juce::String hashFile (const juce::File& audioFile);

    // Returns true and fills `out` on a hit (schema-version matched).
    bool lookup (const juce::String& contentHash, TrackAnalysis& out) const;

    // Insert or replace the cached analysis for this hash.
    void store (const juce::String& contentHash, const TrackAnalysis& analysis);

    int64_t count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisCache)
};
