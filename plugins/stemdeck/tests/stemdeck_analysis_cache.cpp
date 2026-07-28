// AnalysisCache round-trip: store a populated TrackAnalysis, reopen the DB, look
// it up, and assert every field (incl. the beat-grid / tempo-curve blobs) comes
// back identical. Also checks a miss, a hash is stable, and content changes the
// hash.
#include "AnalysisCache.h"
#include "TrackAnalysis.h"
#include <iostream>
#include <cmath>
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

    // ── §6.2 ANALYSIS PINNING (A1) ───────────────────────────────────────────
    // The property the whole gate rests on: a background refinement publishes a NEWER
    // analysis, and the one the live pass actually used must still be retrievable
    // exactly. Before this, the cache was keyed on hash alone and a refinement
    // overwrote it — a replay would silently get a different beat grid and diverge.
    {
        AnalysisCache pc (dbFile);
        const juce::String hash = "pin-test-hash";

        TrackAnalysis v1;
        v1.analysisVersion = 1;
        v1.sampleRate = 44100.0; v1.detectedBpm = 124.0f; v1.downbeatSample = 1000.0;
        v1.keyPitchClass = 9; v1.keyIsMinor = true; v1.keyConfidence = 0.51f;
        v1.beatGridSamples = { 1000.0, 22225.0, 43450.0 };
        pc.store (hash, v1);

        // Refinement lands later with a DIFFERENT grid and key.
        TrackAnalysis v2 = v1;
        v2.analysisVersion = 2;
        v2.detectedBpm = 124.5f; v2.downbeatSample = 1024.0;
        v2.keyPitchClass = 4; v2.keyConfidence = 0.88f;
        v2.beatGridSamples = { 1024.0, 22300.0, 43600.0 };
        pc.store (hash, v2);

        TrackAnalysis got1, got2, latest;
        const bool hit1 = pc.lookup (hash, got1, 1);
        const bool hit2 = pc.lookup (hash, got2, 2);
        const bool hitL = pc.lookup (hash, latest, 0);

        check (hit1, "pinned v1 still retrievable after v2 was stored");
        check (hit2, "refined v2 retrievable");
        check (hitL, "version 0 resolves to the newest");

        check (got1.analysisVersion == 1 && got2.analysisVersion == 2,
                "each lookup returns the version it asked for");
        check (std::abs (got1.detectedBpm - 124.0f) < 1.0e-4f,
                "v1 keeps its OWN bpm, not the refined one");
        check (got1.keyPitchClass == 9 && got2.keyPitchClass == 4,
                "v1 and v2 keep their own keys");
        check (got1.beatGridSamples.size() == 3
                && std::abs (got1.beatGridSamples[0] - 1000.0) < 1.0e-9,
                "v1 keeps its OWN beat grid - a replay gets the grid it performed to");
        check (latest.analysisVersion == 2, "newest is v2");
        check (pc.latestVersion (hash) == 2, "latestVersion reports 2");
        check (pc.latestVersion ("no-such-hash") == 0, "latestVersion is 0 for unknown material");
    }

    // The 64-bit id used in the event stream must agree with the text key.
    {
        const auto a = AnalysisCache::hashToUint64 ("abc");
        const auto b = AnalysisCache::hashToUint64 ("abc");
        const auto c = AnalysisCache::hashToUint64 ("abd");
        check (a == b, "hashToUint64 is stable");
        check (a != c, "hashToUint64 separates different hashes");
        check (a != 0, "hashToUint64 is non-zero");
    }

    std::cout << (fails == 0 ? "ANALYSIS_CACHE_OK" : "ANALYSIS_CACHE_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
