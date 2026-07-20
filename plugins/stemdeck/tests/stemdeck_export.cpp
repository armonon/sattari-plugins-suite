// SessionExport check: render a session, write a WAV, read it back and confirm it
// matches the rendered buffer; and generate a tracklist + .cue whose timestamps
// come from the deckLoad events.
#include "SessionExport.h"
#include "SessionCapture.h"
#include <iostream>
#include <string>
#include <cmath>
#include <memory>

using namespace stemdeck::session;

namespace
{
int fails = 0;
void check (bool ok, const std::string& what)
{
    std::cout << "  " << (ok ? "OK  " : "FAIL") << "  " << what << "\n";
    if (! ok) ++fails;
}

juce::AudioBuffer<float> sineTrack (double sr, double secs, double hz)
{
    const int n = (int) (sr * secs);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * (float) hz * (float) i / (float) sr);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}
} // namespace

int main()
{
    const double SR = 48000.0;
    auto track = sineTrack (SR, 40.0, 300.0);
    SessionRenderer::AudioProvider provider = [&] (const juce::String&, double& r) { r = SR; return &track; };

    // Two tracks entering at beats 0 and 32.
    SessionCapture cap; cap.begin (SR, 124.0);
    cap.recordDeckLoad (0, { "A", "/music/first-track.wav",  "ha", 40, 124, 0, 0, false }, 0.0);
    cap.recordParam (0, "fader", 1.0, 0.0);
    cap.recordTransport (0, TransportKind::play, 0.0, 0.0);
    cap.recordDeckLoad (1, { "B", "/music/second-track.wav", "hb", 40, 126, 0, 0, false }, 32.0);
    cap.recordParam (1, "fader", 1.0, 32.0);
    cap.recordTransport (1, TransportKind::play, 0.0, 32.0);
    cap.recordTransport (0, TransportKind::pause, 0.0, 48.0);
    cap.recordTransport (1, TransportKind::pause, 0.0, 48.0);
    const auto s = cap.session();

    // ── WAV: render, write, read back, compare ──────────────────────────────────
    {
        const auto ref = SessionRenderer::render (s, provider);
        const auto wavFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("stemdeck_export_" + juce::String (juce::Time::currentTimeMillis()) + ".wav");
        check (SessionExport::renderToWav (s, provider, wavFile, 24), "renderToWav writes a file");
        check (wavFile.existsAsFile() && wavFile.getSize() > 1000, "WAV exists and is non-trivial");

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (wavFile));
        check (rd != nullptr, "WAV is readable");
        if (rd != nullptr)
        {
            check ((int) rd->numChannels == 2 && std::abs ((double) rd->sampleRate - SR) < 1.0, "WAV has 2ch @ 48k");
            check (std::abs ((juce::int64) rd->lengthInSamples - ref.getNumSamples()) <= 1, "WAV length == rendered length");
            juce::AudioBuffer<float> back (2, (int) rd->lengthInSamples);
            rd->read (&back, 0, (int) rd->lengthInSamples, 0, true, true);
            double maxErr = 0.0;
            const int n = juce::jmin (back.getNumSamples(), ref.getNumSamples());
            for (int i = 0; i < n; ++i)
                maxErr = juce::jmax (maxErr, (double) std::abs (back.getSample (0, i) - ref.getSample (0, i)));
            check (maxErr < 1.0e-4, "WAV read-back matches render within 24-bit quantisation");
        }
        wavFile.deleteFile();
    }

    // ── Tracklist: mm:ss + name from deckLoad events ────────────────────────────
    {
        const auto tl = SessionExport::tracklist (s);
        // beat 0 -> 00:00 ; beat 32 @124 = 15.48s -> 00:15
        check (tl.contains ("00:00") && tl.contains ("first-track.wav"), "tracklist: first track at 00:00");
        check (tl.contains ("00:15") && tl.contains ("second-track.wav"), "tracklist: second track at 00:15 (beat 32 @124)");
    }

    // ── Cue sheet: valid structure + INDEX timestamps ───────────────────────────
    {
        const auto cue = SessionExport::cueSheet (s, "mix.wav");
        check (cue.contains ("FILE \"mix.wav\" WAVE"), "cue: FILE line");
        check (cue.contains ("TRACK 01 AUDIO") && cue.contains ("TRACK 02 AUDIO"), "cue: two TRACK entries");
        check (cue.contains ("INDEX 01 00:00:00"), "cue: track 1 index at 00:00:00");
        check (cue.contains ("INDEX 01 00:15"), "cue: track 2 index near 00:15");
        check (cue.contains ("first-track.wav") && cue.contains ("second-track.wav"), "cue: titles present");
    }

    std::cout << (fails == 0 ? "EXPORT_OK" : "EXPORT_FAIL (" + std::to_string (fails) + ")") << "\n";
    return fails == 0 ? 0 : 1;
}
