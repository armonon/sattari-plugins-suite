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

    // The same content hash as a 64-bit value, for the event stream's trackLoad
    // payload. Derived from the string form so the two can never disagree.
    static uint64_t hashFile64 (const juce::File& audioFile);
    static uint64_t hashToUint64 (const juce::String& contentHash);

    // Returns true and fills `out` on a hit. analysisVersion 0 means "the highest
    // version stored"; any other value asks for that EXACT analysis, which is what a
    // replay does — it must not silently get a newer, different one (§6.2).
    bool lookup (const juce::String& contentHash, TrackAnalysis& out,
                 uint32_t analysisVersion = 0) const;

    // Store this analysis under (hash, analysis.analysisVersion). Storing a refined
    // result does NOT destroy the earlier one a captured session may depend on.
    void store (const juce::String& contentHash, const TrackAnalysis& analysis);

    // Highest analysisVersion stored for this hash, or 0 if none.
    uint32_t latestVersion (const juce::String& contentHash) const;

    int64_t count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisCache)
};
