// AnalysisCache round-trip: store a populated TrackAnalysis, reopen the DB, look
// it up, and assert every field (incl. the beat-grid / tempo-curve blobs) comes
// back identical. Also checks a miss, a hash is stable, and content changes the
// hash.
#include "AnalysisCache.h"
#include "TrackAnalysis.h"
#include <iostream>
#include <string>

namespace
{
int fails = 0;
void check (bool ok, const std::string& what)
{
    std::cout << "  " << (ok ? "OK  " : "FAIL") << "  " << what << "\n";
    if (! ok) ++fails;
}

TrackAnalysis makeAnalysis()
{
    TrackAnalysis a;
    a.sampleRate = 44100.0; a.durationSeconds = 212.5; a.rmsDb = -9.3f;
    a.detectedBpm = 123.98f; a.downbeatSample = 1649.0; a.beatsPerBar = 4;
    a.keyPitchClass = 7; a.keyIsMinor = true; a.keyConfidence = 0.66f;
    for (int i = 0; i < 400; ++i) a.beatGridSamples.push_back (i * 21337.5);
    for (int i = 0; i < 40; ++i)  a.transientAnchorSamples.push_back (i * 100000.0 + 3.0);
    for (int i = 0; i < 24; ++i)  a.tempoCurve.emplace_back (i * 44100.0, 120.0f + (float) i * 0.5f);
    return a;
}
} // namespace

int main()
{
    const auto dbFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("stemdeck_cache_test_" + juce::String (juce::Time::currentTimeMillis()) + ".db");
    dbFile.deleteFile();

    const auto in = makeAnalysis();
    {
        AnalysisCache c (dbFile);
        check (c.isOpen(), "cache DB opens");
        TrackAnalysis miss;
        check (! c.lookup ("deadbeef", miss), "lookup of unknown hash misses");
        c.store ("abc123", in);
        check (c.count() == 1, "one row after store");
    }

    // Reopen to prove persistence across sessions.
    {
        AnalysisCache c (dbFile);
        TrackAnalysis out;
        const bool hit = c.lookup ("abc123", out);
        check (hit, "reopened DB: stored hash hits");
        check (out.sampleRate == in.sampleRate && out.durationSeconds == in.durationSeconds, "scalars: sr/dur");
        check (out.detectedBpm == in.detectedBpm && out.downbeatSample == in.downbeatSample
               && out.beatsPerBar == in.beatsPerBar, "scalars: bpm/downbeat/meter");
        check (out.keyPitchClass == in.keyPitchClass && out.keyIsMinor == in.keyIsMinor
               && out.keyConfidence == in.keyConfidence, "scalars: key");
        check (out.beatGridSamples == in.beatGridSamples, "blob: beat grid (400 doubles) round-trips");
        check (out.transientAnchorSamples == in.transientAnchorSamples, "blob: transients round-trip");
        check (out.tempoCurve == in.tempoCurve, "blob: tempo curve (24 pairs) round-trips");
    }

    // ── Content hashing ────────────────────────────────────────────────────────
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("stemdeck_hash_test.bin");
    tmp.replaceWithData (juce::MemoryBlock ("hello world stemdeck", 20).getData(), 20);
    const auto h1 = AnalysisCache::hashFile (tmp);
    const auto h2 = AnalysisCache::hashFile (tmp);
    check (h1.isNotEmpty() && h1 == h2, "hash is non-empty and stable");
    tmp.replaceWithData (juce::MemoryBlock ("hello world stemdeckX", 21).getData(), 21);
    check (AnalysisCache::hashFile (tmp) != h1, "content change changes the hash");
    tmp.deleteFile();

    dbFile.deleteFile();
    juce::File (dbFile.getFullPathName() + "-wal").deleteFile();
    juce::File (dbFile.getFullPathName() + "-shm").deleteFile();

    std::cout << (fails == 0 ? "ANALYSIS_CACHE_OK" : "ANALYSIS_CACHE_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
