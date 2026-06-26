// Auto Pitch — offline render + null-test driver.
//
// Renders a deterministic vocal-like signal through Auto Pitch in a fixed
// hard-tune configuration, writes a .wav you can listen to, and null-tests the
// render fingerprint against a committed golden. If the DSP output drifts
// between commits, the residual spikes and this fails.
//
// Usage:
//   auto_pitch_render            # render + strict null-test against golden (local
//                                #   dev regression gate; same machine + Rubber Band)
//   auto_pitch_render --bless     # render + (over)write the golden fingerprint
//   auto_pitch_render --smoke     # render + assert finite/non-silent only; NO golden
//                                #   compare. CI-safe (Rubber Band output can differ
//                                #   across versions/platforms, so a strict null is
//                                #   not portable - smoke catches crashes/silence).
//
// Paths are resolved relative to the repo root via SATTARI_REPO_ROOT (set by the
// CMake target) so the .wav and golden land in validation/ regardless of CWD.
#include "PluginProcessor.h"
#include <sattari_audio_core/SattariRenderHarness.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        std::fprintf (stderr, "WARNING: missing parameter %s\n", id.toRawUTF8());
}

juce::File repoRoot()
{
   #ifdef SATTARI_REPO_ROOT
    return juce::File (SATTARI_REPO_ROOT);
   #else
    return juce::File::getCurrentWorkingDirectory();
   #endif
}
} // namespace

int main (int argc, char** argv)
{
    const bool bless = (argc > 1 && std::strcmp (argv[1], "--bless") == 0);
    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);

    const double sr = 48000.0;
    const int    block = 512;
    const double seconds = 4.0;

    // Deterministic vocal-like input.
    juce::AudioBuffer<float> input;
    sattari::harness::generateVocalLike (input, sr, seconds);

    // Fixed, representative configuration: hard-tune to C major, YIN engine,
    // formant preserve on, transition modeling at default. This is the "voice
    // print" we lock - any DSP change to the correction path moves it.
    SattariAutoPitchAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "keyRoot", 0.0f);          // C
    setParam (apvts, "scale", 0.0f);            // major
    setParam (apvts, "style", 2.0f);            // Modern Rap (hard)
    setParam (apvts, "amount", 1.0f);
    setParam (apvts, "speed", 0.9f);
    setParam (apvts, "enableCorrection", 1.0f);
    setParam (apvts, "doNoHarm", 0.0f);
    setParam (apvts, "formantPreserve", 1.0f);
    setParam (apvts, "classicMode", 0.0f);
    setParam (apvts, "flexTune", 0.0f);
    setParam (apvts, "transition", 0.5f);
    setParam (apvts, "throat", 1.0f);

    const auto out = sattari::harness::renderThrough (proc, input, sr, block);

    const auto root    = repoRoot();
    const auto wavFile = root.getChildFile ("validation/renders/auto_pitch_hardtune_Cmaj.wav");
    const auto goldFile = root.getChildFile ("validation/golden/auto_pitch_hardtune_Cmaj.fingerprint");

    if (! sattari::harness::writeWav (wavFile, out, sr))
        { std::fprintf (stderr, "FAIL: could not write %s\n", wavFile.getFullPathName().toRawUTF8()); return 2; }
    std::printf ("rendered %.1fs -> %s\n", seconds, wavFile.getFullPathName().toRawUTF8());

    const auto fp = sattari::harness::fingerprint (out, sr);
    std::printf ("  peak=%.4f rms=%.4f centroidHz=%.1f\n", fp.peak, fp.rms, fp.centroidHz);

    if (smoke)
    {
        // CI-safe: just prove the render path produced finite, non-silent audio.
        bool finite = true;
        for (int ch = 0; ch < out.getNumChannels() && finite; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
                if (! std::isfinite (out.getSample (ch, i))) { finite = false; break; }
        if (! finite)            { std::fprintf (stderr, "FAIL: render produced non-finite samples\n"); return 1; }
        if (fp.rms < 1.0e-3f)    { std::fprintf (stderr, "FAIL: render is silent (rms %.6f)\n", fp.rms); return 1; }
        std::printf ("Auto Pitch render smoke ok\n");
        return 0;
    }

    if (bless)
    {
        goldFile.getParentDirectory().createDirectory();
        goldFile.replaceWithText (sattari::harness::fingerprintToText (fp));
        std::printf ("BLESSED golden -> %s\n", goldFile.getFullPathName().toRawUTF8());
        return 0;
    }

    if (! goldFile.existsAsFile())
    {
        std::fprintf (stderr, "FAIL: no golden fingerprint at %s (run once with --bless)\n",
                      goldFile.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto golden = sattari::harness::fingerprintFromText (goldFile.loadFileAsString());
    const float residual = sattari::harness::maxResidual (golden, fp);

    // Tolerance: the render is fully deterministic, so identical code must null
    // to ~0. A small floor absorbs float/-O variation across compilers.
    constexpr float tol = 1.0e-4f;
    std::printf ("  null residual vs golden = %.6g (tol %.6g)\n", residual, tol);
    if (residual > tol)
    {
        std::fprintf (stderr,
            "FAIL: render drifted from golden (residual %.6g > %.6g).\n"
            "      If this change to the sound is intended, re-bless: auto_pitch_render --bless\n",
            residual, tol);
        return 1;
    }

    std::printf ("Auto Pitch render null-test ok\n");
    return 0;
}
