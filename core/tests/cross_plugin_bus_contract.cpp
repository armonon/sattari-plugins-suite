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
#include <cstdlib>
#include <chrono>
#include <thread>

using namespace sattari::xplugin;

int main()
{
    // Never contend with a real DAW session (or another test process) while
    // exercising publisher leases and stale-state behavior.
    const auto contractSession = "contract-" + juce::String (currentTimeMs());
   #if JUCE_WINDOWS
    _putenv_s ("SATTARI_SESSION_ID", contractSession.toRawUTF8());
   #else
    setenv ("SATTARI_SESSION_ID", contractSession.toRawUTF8(), 1);
   #endif

    int fails = 0;
    auto check = [&fails] (bool ok, const char* what)
    {
        if (! ok) { std::fprintf (stderr, "FAIL: %s\n", what); ++fails; }
    };

    // 1) Session round-trip.
    check (publishSession ("auto-pitch", /*root*/ 7, /*scale*/ 1,
                           /*bpm*/ 128.0, /*transientId*/ 42),
           "initial session publish failed");
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
    publishKey ("auto-pitch", /*root*/ 2, /*scale*/ 0);
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

    // 5) Publisher leases: a fresh authoritative Brain frame cannot be
    //    overwritten by a lower-priority detector. Brain can update its own
    //    frame, and readers see the surviving coherent state.
    {
        SessionMessage authoritative;
        authoritative.root = 11;
        authoritative.scale = 1;
        authoritative.bpm = 111.0;
        authoritative.chordRoot = 4;
        check (publishSessionState ("brain", authoritative),
               "Brain authority publish failed");
        check (! publishSession ("auto-pitch", 5, 0, 96.0, 7),
               "lower-priority publisher stole a fresh Brain lease");
        auto surviving = readSession();
        check (surviving && surviving->root == 11 && surviving->chordRoot == 4,
               "Brain frame did not survive lower-priority publish");
    }

    // 6) Two SessionReader clients share the per-binary poller and both receive
    //    the same coherent lock-free frame (no audio-thread file I/O).
    {
        publishSession ("brain", /*root*/ 5, /*scale*/ 1, /*bpm*/ 96.0, /*transientId*/ 7);
        SessionReader readerA, readerB;
        readerA.start (/*pollIntervalMs*/ 40);
        readerB.start (/*pollIntervalMs*/ 80);
        SessionReader::Snapshot snapA, snapB;
        for (int i = 0; i < 40; ++i)
        {
            snapA = readerA.current();
            snapB = readerB.current();
            if (snapA.valid && snapB.valid) break;
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        readerA.stop();
        readerB.stop();
        check (snapA.valid && snapB.valid, "SessionReader clients missed session");
        check (snapA.root == 5 && snapA.scale == 1, "SessionReader snapshot key wrong");
        check (std::abs (snapA.bpm - 96.0) < 1e-6, "SessionReader snapshot bpm wrong");
        check (snapA.transientId == 7, "SessionReader snapshot transientId wrong");
        check (snapA.root == snapB.root && snapA.transientId == snapB.transientId,
               "shared SessionReader clients observed different frames");
    }

    // 7) Tempo channel is INDEPENDENT of the key bus: publishing tempo does not
    //    touch the key, and a TempoReader picks it up. Publishing the key does not
    //    wipe the tempo (separate files).
    {
        publishKey ("brain", /*root*/ 3, /*scale*/ 0);
        publishTempo ("rhythm-genie", /*bpm*/ 140.0, /*transientId*/ 9);

        auto k = readPublishedKey();
        check (k.has_value() && k->root == 3, "key lost after tempo publish");
        auto t = readTempo();
        check (t.has_value(), "tempo not readable");
        if (t) { check (std::abs (t->bpm - 140.0) < 1e-6 && t->transientId == 9, "tempo values wrong"); }

        publishKey ("brain", /*root*/ 8, /*scale*/ 1);   // re-publish key
        auto t2 = readTempo();
        check (t2.has_value() && std::abs (t2->bpm - 140.0) < 1e-6, "key re-publish clobbered tempo");

        TempoReader tr; tr.start (40);
        TempoReader::Snapshot ts;
        for (int i = 0; i < 40; ++i) { ts = tr.current(); if (ts.valid) break; std::this_thread::sleep_for (std::chrono::milliseconds (20)); }
        tr.stop();
        check (ts.valid && std::abs (ts.bpm - 140.0) < 1e-6 && ts.transientId == 9, "TempoReader snapshot wrong");
    }

    // 8) SessionState v2+: publishSessionState round-trips the full Brain context
    //    (chord/timeSig/swing/section/energy/maqam/reference) and additive
    //    Dynamic Key metadata, AND a plain v1 publishSession leaves the newer
    //    fields at safe defaults (backward compat).
    {
        SessionMessage full;
        full.root = 2; full.scale = 1; full.bpm = 100.0; full.transientId = 3;
        full.chordRoot = 9; full.chordQuality = 4; full.timeSigNum = 6; full.timeSigDen = 8;
        full.swing = 0.6f; full.sectionId = 4; full.energy = 0.8f; full.maqamId = 5;
        full.referenceHz = 432.0; full.progression = "2,9,5,7";
        full.dynamicKey = true; full.keyConfidence = 0.73f;
        full.keyRevision = 6; full.keyWindowSeconds = 2.5;
        publishSessionState ("brain", full);
        auto r = readSession();
        check (r.has_value(), "SessionState not readable");
        if (r)
        {
            check (r->chordRoot == 9 && r->chordQuality == 4, "chord round-trip wrong");
            check (r->timeSigNum == 6 && r->timeSigDen == 8, "timeSig round-trip wrong");
            check (std::abs (r->swing - 0.6f) < 1e-4 && std::abs (r->energy - 0.8f) < 1e-4, "swing/energy wrong");
            check (r->sectionId == 4 && r->maqamId == 5, "section/maqam wrong");
            check (std::abs (r->referenceHz - 432.0) < 1e-6, "referenceHz wrong");
            check (r->progression == "2,9,5,7", "progression wrong");
            check (r->dynamicKey, "dynamic-key flag missing");
            check (std::abs (r->keyConfidence - 0.73f) < 1e-4,
                   "dynamic-key confidence wrong");
            check (r->keyRevision == 6, "dynamic-key revision wrong");
            check (std::abs (r->keyWindowSeconds - 2.5) < 1e-6,
                   "dynamic-key window wrong");
        }

        SessionReader dynamicReader;
        dynamicReader.start (40);
        SessionReader::Snapshot dynamicSnapshot;
        for (int i = 0; i < 40; ++i)
        {
            dynamicSnapshot = dynamicReader.current();
            if (dynamicSnapshot.valid && dynamicSnapshot.keyRevision == 6) break;
            std::this_thread::sleep_for (std::chrono::milliseconds (20));
        }
        dynamicReader.stop();
        check (dynamicSnapshot.valid && dynamicSnapshot.dynamicKey
                   && dynamicSnapshot.keyRevision == 6
                   && std::abs (dynamicSnapshot.keyConfidence - 0.73f) < 1e-4
                   && std::abs (dynamicSnapshot.keyWindowSeconds - 2.5) < 1e-6,
               "SessionReader dynamic-key snapshot wrong");

        // A v1 key+tempo-only publish must leave the v2 fields at defaults.
        publishSession ("brain", 0, 0, 120.0, 1);
        auto d = readSession();
        check (d.has_value(), "v1 session not readable");
        if (d)
        {
            check (d->chordRoot == -1 && d->chordQuality == 0, "v1 default chord wrong");
            check (d->timeSigNum == 4 && d->timeSigDen == 4, "v1 default timeSig wrong");
            check (std::abs (d->referenceHz - 440.0) < 1e-6, "v1 default reference wrong");
            check (! d->dynamicKey && d->keyConfidence == 0.0f
                       && d->keyRevision == 0 && d->keyWindowSeconds == 0.0,
                   "v1 default dynamic-key metadata wrong");
        }
    }

    // 9) Schema v3 project namespaces isolate concurrently-open DAW sessions.
    //    The unscoped legacy path remains available when no id is supplied.
    {
        const auto unique = juce::String (currentTimeMs());
        const auto sessionA = "contract-a-" + unique;
        const auto sessionB = "contract-b-" + unique;

        check (publishSession ("brain", 1, 0, 101.0, 11, sessionA),
               "session A publish failed");
        check (publishSession ("brain", 10, 1, 139.0, 22, sessionB),
               "session B publish failed");

        const auto a = readSession (30, sessionA);
        const auto b = readSession (30, sessionB);
        check (a && a->root == 1 && std::abs (a->bpm - 101.0) < 1e-6,
               "session A leaked or was not readable");
        check (b && b->root == 10 && std::abs (b->bpm - 139.0) < 1e-6,
               "session B leaked or was not readable");
        check (a && a->schemaVersion == 3 && a->sessionId == sessionA,
               "session A v3 namespace metadata missing");
        check (keyBusFile (sessionA) != keyBusFile (sessionB),
               "namespaced key files must differ");

        publishTempo ("rhythm-genie", 101.0, 1, sessionA);
        publishTempo ("rhythm-genie", 139.0, 2, sessionB);
        const auto tempoA = readTempo (10, sessionA);
        const auto tempoB = readTempo (10, sessionB);
        check (tempoA && tempoB && tempoA->transientId == 1 && tempoB->transientId == 2,
               "tempo namespaces leaked across sessions");

        keyBusFile (sessionA).deleteFile();
        keyBusFile (sessionB).deleteFile();
        tempoBusFile (sessionA).deleteFile();
        tempoBusFile (sessionB).deleteFile();
    }

    // 10) A publisher can withdraw its own frame immediately when the user
    //     disables/unlocks it, but cannot delete another authority's frame.
    {
        const auto session = "contract-withdraw-" + juce::String (currentTimeMs());
        check (publishSession ("song-key", 4, 0, 0.0, 0, session),
               "withdrawal fixture publish failed");
        check (! withdrawSessionState ("auto-pitch", session),
               "non-owner withdrew another publisher's frame");
        check (readSession (30, session).has_value(),
               "non-owner withdrawal removed the frame");
        check (withdrawSessionState ("song-key", session),
               "owner could not withdraw its frame");
        check (! readSession (30, session).has_value(),
               "withdrawn frame remained readable");
    }

    keyBusFile().deleteFile();
    tempoBusFile().deleteFile();

    if (fails != 0) { std::fprintf (stderr, "Cross-plugin bus contract FAILED (%d)\n", fails); return 1; }
    std::printf ("Cross-plugin session bus contract ok\n");
    return 0;
}
