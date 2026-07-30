// Sattari Compass — functional contract (legacy Brain binary identity).
//
// Compass is the session conductor: it detects key + reference tuning, lets the user
// edit the rest of the SessionState, and publishes the FULL v2 SessionState to
// the cross-plugin bus while passing audio through untouched. This proves:
//   1. audio pass-through is bit-exact (Compass never touches the signal);
//   2. editing chord / time-sig / swing / section / energy / maqam + Publish
//      makes readSession() return exactly those v2 fields;
//   3. a manual key override publishes that key;
//   4. a reference override publishes that referenceHz;
//   5. nothing is NaN / non-finite.
//
// The publish path runs on Compass's ~700 ms worker thread (as it does in a DAW),
// so each edit is verified by polling readSession() until it reflects the edit
// (with a timeout), the same pattern the bus's SessionReader contract uses.
#include "BrainProcessor.h"
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariEditorRender.h>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>
#include <functional>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Poll the bus until `pred(msg)` holds or we time out. Returns the last-read
// message (so callers can assert on it) and whether the predicate was met.
bool waitForSession (const std::function<bool (const sattari::xplugin::SessionMessage&)>& pred,
                     sattari::xplugin::SessionMessage& out, int timeoutMs = 6000)
{
    const int stepMs = 50;
    for (int waited = 0; waited <= timeoutMs; waited += stepMs)
    {
        if (auto s = sattari::xplugin::readSession())
        {
            out = *s;
            if (pred (out)) return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (stepMs));
    }
    return false;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3 && juce::String (argv[1]) == "--render-ui")
    {
        SattariBrainAudioProcessor processor;
        const juce::File destination (argv[2]);
        const bool ok = sattari::test::renderEditorToPng (processor, destination);
        std::printf ("%s %s\n", ok ? "rendered" : "FAIL render",
                     destination.getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }

    int fails = 0;
    auto check = [&fails] (bool ok, const char* what)
    {
        if (! ok) { std::fprintf (stderr, "FAIL: %s\n", what); ++fails; }
    };

    SattariBrainAudioProcessor proc;
    auto& a = proc.parameters();
    const double sr = 48000.0; const int block = 512;
    proc.prepareToPlay (sr, block);

    // 1) Bit-exact pass-through + finite. Feed a known stereo signal; Compass must
    //    return it untouched, sample for sample.
    bool allFinite = true, bitExact = true;
    {
        const int n = block * 8;
        juce::MidiBuffer midi;
        double ph = 0.0;
        for (int start = 0; start < n; start += block)
        {
            juce::AudioBuffer<float> buf (2, block);
            juce::AudioBuffer<float> copy (2, block);
            for (int i = 0; i < block; ++i)
            {
                const float l = 0.6f * (float) std::sin (ph);
                const float rr = 0.4f * (float) std::sin (ph * 1.5);
                buf.setSample (0, i, l);  copy.setSample (0, i, l);
                buf.setSample (1, i, rr); copy.setSample (1, i, rr);
                ph += 2.0 * M_PI * 220.0 / sr;
            }
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) allFinite = false;
                    if (v != copy.getSample (ch, i)) bitExact = false;
                }
        }
    }
    check (bitExact, "audio pass-through not bit-exact");
    check (allFinite, "pass-through produced non-finite samples");

    // 2) Edit the SessionState fields; the published SessionState must carry them.
    //    Choice indices: chordRoot 10 -> A (pc 9); chordQuality 4 -> min7;
    //    timeSigDen 3 -> 8; section 4 -> chorus (id 4).
    // Use an explicit key while testing edited context. Automatic mode now
    // correctly stays silent until a confident key commits; context must never
    // be attached to an invented startup C major.
    setParam (a, "brainManual",      1.0f);
    setParam (a, "brainRefOverride", 0.0f);
    setParam (a, "brainPublish",     1.0f);
    setParam (a, "brainChordRoot",    10.0f);
    setParam (a, "brainChordQuality", 4.0f);
    setParam (a, "brainTimeSigNum",   6.0f);
    setParam (a, "brainTimeSigDen",   3.0f);
    setParam (a, "brainSwing",        0.6f);
    setParam (a, "brainSection",      4.0f);
    setParam (a, "brainEnergy",       0.8f);
    setParam (a, "brainMaqam",        5.0f);
    {
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession ([] (const sattari::xplugin::SessionMessage& s)
        {
            return s.chordRoot == 9 && s.chordQuality == 4
                && s.timeSigNum == 6 && s.timeSigDen == 8
                && std::abs (s.swing - 0.6f) < 1.0e-3f
                && s.sectionId == 4
                && std::abs (s.energy - 0.8f) < 1.0e-3f
                && s.maqamId == 5;
        }, m);
        check (ok, "edited SessionState fields were not published");
        check (m.chordRoot == 9 && m.chordQuality == 4, "chord round-trip wrong");
        check (m.timeSigNum == 6 && m.timeSigDen == 8, "time signature round-trip wrong");
        check (std::abs (m.swing - 0.6f) < 1.0e-3f, "swing round-trip wrong");
        check (m.sectionId == 4, "section round-trip wrong");
        check (std::abs (m.energy - 0.8f) < 1.0e-3f, "energy round-trip wrong");
        check (m.maqamId == 5, "maqam round-trip wrong");
        check (m.transientId == 0,
               "Compass published a timer tick as a musical transient");
        check (std::isfinite (m.referenceHz) && std::isfinite (m.swing) && std::isfinite (m.energy),
               "published SessionState has non-finite fields");
    }

    // 3) Manual key override wins over detection: publishes exactly that key.
    setParam (a, "brainManual",      1.0f);
    setParam (a, "brainManualRoot",  7.0f);   // G
    setParam (a, "brainManualScale", 1.0f);   // minor
    {
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession ([] (const sattari::xplugin::SessionMessage& s)
        {
            return s.root == 7 && s.scale == 1;
        }, m);
        check (ok, "manual key override was not published");
    }

    // 4) Reference override: publishes exactly that referenceHz.
    setParam (a, "brainRefOverride", 1.0f);
    setParam (a, "brainReferenceHz", 432.0f);
    {
        sattari::xplugin::SessionMessage m;
        const bool ok = waitForSession ([] (const sattari::xplugin::SessionMessage& s)
        {
            return std::abs (s.referenceHz - 432.0) < 0.5;
        }, m);
        check (ok, "reference override was not published");
    }

    proc.releaseResources();

    if (fails != 0) { std::fprintf (stderr, "Sattari Compass contract FAILED (%d)\n", fails); return 1; }
    std::printf ("Sattari Compass contract ok\n");
    return 0;
}
