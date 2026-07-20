// StemDeck session demo — exercises the whole capture -> render -> export spine on
// real audio and writes files you can open and play. Generates two short tracks,
// "performs" a mix (load A, crossfade to B over a transition, an EQ move), then:
//   * renders a mastered WAV at the performed tempo,
//   * RE-EXPORTS the same session at a different tempo (the headline feature),
//   * meters both (LUFS / true-peak),
//   * writes a tracklist + .cue.
//
//   StemDeckSessionDemo [outputDir]      (default: ~/Desktop/StemDeck-demo)
#include "SessionCapture.h"
#include "SessionRenderer.h"
#include "SessionExport.h"
#include "LoudnessMeter.h"
#include <iostream>
#include <memory>

using namespace stemdeck::session;

namespace
{
// A little "track": kick on every beat + a sustained tone, so a mix is audible.
juce::AudioBuffer<float> makeTrack (double sr, double bpm, double toneHz, double secs)
{
    const int n = (int) (sr * secs);
    juce::AudioBuffer<float> b (2, n); b.clear();
    const double beat = sr * 60.0 / bpm;
    for (double t = 0; t < n; t += beat)            // kick
    {
        const int s = (int) t;
        for (int i = 0; i < (int) (sr * 0.12) && s + i < n; ++i)
        {
            const float e = std::exp (-(float) i / (float) (sr * 0.03));
            const float v = 0.6f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * i / (float) sr) * e;
            b.addSample (0, s + i, v); b.addSample (1, s + i, v);
        }
    }
    for (int i = 0; i < n; ++i)                     // tone bed
    {
        const float v = 0.18f * std::sin (2.0f * juce::MathConstants<float>::pi * (float) toneHz * i / (float) sr);
        b.addSample (0, i, v); b.addSample (1, i, v);
    }
    return b;
}
} // namespace

int main (int argc, char** argv)
{
    const double SR = 44100.0;
    const double performTempo = 124.0;

    auto trackA = makeTrack (SR, performTempo, 220.0, 40.0);   // A: 220 Hz bed
    auto trackB = makeTrack (SR, performTempo, 330.0, 40.0);   // B: 330 Hz bed
    SessionRenderer::AudioProvider provider = [&] (const juce::String& id, double& rate)
    { rate = SR; return id == "A" ? &trackA : &trackB; };

    // ── "Perform" a mix: A in, crossfade to B over 16 beats, an EQ sweep ────────
    SessionCapture cap; cap.begin (SR, performTempo);
    cap.recordDeckLoad (0, { "A", "/demo/track-a.wav", "ha", 40, performTempo, 0, 0, false }, 0.0);
    cap.recordParam (0, "cfSide", 0.0, 0.0);         // deck A -> bus A
    cap.recordParam (0, "fader", 1.0, 0.0);
    cap.recordParam (0, "crossfader", 0.0, 0.0);     // full A
    cap.recordTransport (0, TransportKind::play, 0.0, 0.0);

    cap.recordDeckLoad (1, { "B", "/demo/track-b.wav", "hb", 40, performTempo, 0, 0, false }, 32.0);
    cap.recordParam (1, "cfSide", 2.0, 32.0);        // deck B -> bus B
    cap.recordParam (1, "fader", 1.0, 32.0);
    cap.recordTransport (1, TransportKind::play, 0.0, 32.0);

    // The transition: ramp the crossfader A->B across beats 32..48, and roll off A's lows.
    Region tr; tr.kind = "transition"; tr.startBeat = 32.0; tr.endBeat = 48.0;
    for (int k = 0; k <= 8; ++k)
    {
        const double beat = 32.0 + k * 2.0;
        cap.recordParam (0, "crossfader", k / 8.0, beat);
        cap.recordParam (0, "low", -3.0 * k, beat);   // bass out of the outgoing track
        tr.eventIds.push_back (0);                     // (ids illustrative)
    }
    cap.session().addRegion (tr);

    cap.recordTransport (0, TransportKind::pause, 0.0, 56.0);   // A out
    cap.recordTransport (1, TransportKind::pause, 0.0, 64.0);   // B out
    cap.end();
    const auto session = cap.session();

    // ── Output dir ──────────────────────────────────────────────────────────────
    const juce::File outDir = argc >= 2 ? juce::File (juce::String (argv[1]))
                                        : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                            .getChildFile ("Desktop").getChildFile ("StemDeck-demo");
    outDir.createDirectory();

    std::cout << "StemDeck session demo\n=====================\n";
    std::cout << "Performed " << session.events.size() << " events, "
              << session.tracks.size() << " tracks, at " << performTempo << " BPM.\n\n";

    // ── Render at the performed tempo ───────────────────────────────────────────
    {
        const auto mix = SessionRenderer::render (session, provider);
        const auto wav = outDir.getChildFile ("mix-124bpm.wav");
        SessionExport::writeWav (mix, SR, wav, 24);
        const auto m = stemdeck::LoudnessMeter::measure (mix, SR);
        std::cout << "[render @ 124 BPM]  " << (double) mix.getNumSamples() / SR << " s  ->  "
                  << wav.getFullPathName() << "\n"
                  << "   integrated " << juce::String (m.integratedLufs, 1) << " LUFS,  true-peak "
                  << juce::String (m.truePeakDb, 1) << " dBTP\n\n";
    }

    // ── Re-export the SAME session at 128 BPM (the headline feature) ────────────
    {
        auto faster = session; faster.setMasterTempo (128.0);
        const auto mix = SessionRenderer::render (faster, provider);
        const auto wav = outDir.getChildFile ("mix-128bpm.wav");
        SessionExport::writeWav (mix, SR, wav, 24);
        const auto m = stemdeck::LoudnessMeter::measure (mix, SR);
        std::cout << "[re-export @ 128 BPM]  " << (double) mix.getNumSamples() / SR << " s  ->  "
                  << wav.getFullPathName() << "\n"
                  << "   integrated " << juce::String (m.integratedLufs, 1) << " LUFS,  true-peak "
                  << juce::String (m.truePeakDb, 1) << " dBTP\n"
                  << "   (same performance, retimed — shorter because it's faster)\n\n";
    }

    // ── Tracklist + cue ─────────────────────────────────────────────────────────
    const auto tl = SessionExport::tracklist (session);
    const auto cue = SessionExport::cueSheet (session, "mix-124bpm.wav");
    outDir.getChildFile ("tracklist.txt").replaceWithText (tl);
    outDir.getChildFile ("mix.cue").replaceWithText (cue);
    session.saveTo (outDir.getChildFile ("session.json"));

    std::cout << "Tracklist:\n" << tl << "\n";
    std::cout << "Cue sheet:\n" << cue << "\n";
    std::cout << "Wrote WAVs, tracklist.txt, mix.cue, session.json to:\n   " << outDir.getFullPathName() << "\n";
    return 0;
}
