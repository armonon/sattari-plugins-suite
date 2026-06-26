// Cross-plugin session bus — round-trip + backward-compat contract.
//
// Proves the file-based session channel that lets suite plugins share state in
// a DAW:
//   1. publishSession -> readSession round-trips key + tempo + transient id.
//   2. A session publish is still readable by the legacy key-only reader
//      (publishKey/readPublishedKey callers keep working).
//   3. A legacy key-only publish is readable by a session reader, with tempo
//      absent (hasTempo == false) rather than garbage.
//   4. Stale entries (older than maxAge) are ignored.
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <cstdio>
#include <chrono>
#include <thread>

using namespace sattari::xplugin;

int main()
{
    int fails = 0;
    auto check = [&fails] (bool ok, const char* what)
    {
        if (! ok) { std::fprintf (stderr, "FAIL: %s\n", what); ++fails; }
    };

    // 1) Session round-trip.
    publishSession ("auto-pitch", /*root*/ 7, /*scale*/ 1, /*bpm*/ 128.0, /*transientId*/ 42);
    {
        auto s = readSession();
        check (s.has_value(), "session not readable");
        if (s)
        {
            check (s->root == 7, "session root mismatch");
            check (s->scale == 1, "session scale mismatch");
            check (std::abs (s->bpm - 128.0) < 1e-6, "session bpm mismatch");
            check (s->transientId == 42, "session transientId mismatch");
            check (s->hasTempo, "session hasTempo should be true");
        }
    }

    // 2) Legacy key reader still works against a session publish.
    {
        auto k = readPublishedKey();
        check (k.has_value(), "legacy key reader could not read session publish");
        if (k) { check (k->root == 7 && k->scale == 1, "legacy key reader wrong values"); }
    }

    // 3) Session reader against a legacy key-only publish: no tempo.
    publishKey ("royal-chain", /*root*/ 2, /*scale*/ 0);
    {
        auto s = readSession();
        check (s.has_value(), "session reader could not read legacy key publish");
        if (s)
        {
            check (s->root == 2 && s->scale == 0, "session reader wrong key from legacy publish");
            check (! s->hasTempo, "session reader should report no tempo for legacy publish");
            check (s->bpm == 0.0, "session reader bpm should be 0 for legacy publish");
        }
    }

    // 4) Stale entries are ignored. Publish, wait past a 0-second window, read.
    {
        publishSession ("auto-pitch", 0, 0, 120.0, 1);
        std::this_thread::sleep_for (std::chrono::milliseconds (30));
        auto s = readSession (/*maxAgeSeconds*/ 0);   // anything older than now is stale
        check (! s.has_value(), "stale session entry should be ignored");
    }

    if (fails != 0) { std::fprintf (stderr, "Cross-plugin bus contract FAILED (%d)\n", fails); return 1; }
    std::printf ("Cross-plugin session bus contract ok\n");
    return 0;
}
