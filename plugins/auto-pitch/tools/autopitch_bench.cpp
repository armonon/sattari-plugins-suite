// autopitch_bench.cpp — offline per-frame trace renderer for AutoPitch
//
// Usage:
//   autopitch_bench input.wav --output out.wav --trace trace.json [options]
//   --key C|C#|...|B         root note (default: A)
//   --scale major|minor|chromatic (default: minor)
//   --amount 0..1            correction amount (default: 0.82)
//   --speed  0..1            retune speed (default: 0.70)
//   --block  N               block size in samples (default: 512)
//   --intent-mode production|v2_shadow|v2_active  (default: production)
//   --smoke                  finite+non-silent check only, no trace required
//
// Trace JSON schema:
//   { "frames": [ { "blockIndex": 0, "timeSec": 0.01, "voiced": true,
//                   "detectedMidi": 57.2, "correctionCents": -20.0,
//                   "confidence": 0.85, "detectedRoot": 9,
//                   "intentTarget": 57, "intentConf": 0.81,
//                   "intentReason": 3, "intentFallback": false } ] }
//
// intentTarget/intentConf/intentReason/intentFallback are -1/0/0/false
// when intentMode == PRODUCTION.

#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{

struct Args
{
    juce::File inputWav;
    juce::File outputWav;
    juce::File traceJson;
    juce::String key   = "A";
    juce::String scale = "minor";
    float  amount      = 0.82f;
    float  speed       = 0.70f;
    int    block       = 512;
    IntentMode intentMode = IntentMode::PRODUCTION;
    bool   smoke       = false;
    bool   valid       = false;
};

static void setParam (juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        std::fprintf (stderr, "WARNING: unknown parameter '%s'\n", id.toRawUTF8());
}

static const char* reasonName (IntentReasonCode c)
{
    switch (c)
    {
        case IntentReasonCode::NEAREST_STABLE:           return "NEAREST_STABLE";
        case IntentReasonCode::DIRECTIONAL_DESTINATION:  return "DIRECTIONAL_DESTINATION";
        case IntentReasonCode::PASSING_TONE:             return "PASSING_TONE";
        case IntentReasonCode::SLIDE_DESTINATION:        return "SLIDE_DESTINATION";
        case IntentReasonCode::STABILITY_ANCHOR:         return "STABILITY_ANCHOR";
        case IntentReasonCode::VIBRATO_HOLD:             return "VIBRATO_HOLD";
        case IntentReasonCode::SLIDE_DESTINATION_V2:     return "SLIDE_DESTINATION_V2";
        default:                                          return "LOW_CONFIDENCE_FALLBACK";
    }
}

static int rootStringToIndex (const juce::String& root)
{
    const juce::StringArray names { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int idx = names.indexOf (root);
    return idx >= 0 ? idx : 9; // default A
}

static Args parseArgs (int argc, char** argv)
{
    Args a;
    if (argc < 2)
    {
        std::fprintf (stderr,
            "usage: autopitch_bench input.wav --output out.wav --trace trace.json [options]\n"
            "  --key C|C#|...|B     root note (default: A)\n"
            "  --scale major|minor|chromatic (default: minor)\n"
            "  --amount 0..1        correction amount (default: 0.82)\n"
            "  --speed  0..1        retune speed (default: 0.70)\n"
            "  --block N            block size in samples (default: 512)\n"
            "  --intent-mode production|v2_shadow|v2_active  (default: production)\n"
            "  --smoke              finite+non-silent only, no --trace required\n");
        return a;
    }

    a.inputWav = juce::File (juce::String (argv[1]));
    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const bool hasNext = (i + 1 < argc);

        if (arg == "--output"  && hasNext) { a.outputWav  = juce::File (juce::String (argv[++i])); }
        else if (arg == "--trace" && hasNext) { a.traceJson  = juce::File (juce::String (argv[++i])); }
        else if (arg == "--key"   && hasNext) { a.key        = juce::String (argv[++i]); }
        else if (arg == "--scale" && hasNext) { a.scale      = juce::String (argv[++i]); }
        else if (arg == "--amount"&& hasNext) { a.amount     = std::stof (argv[++i]); }
        else if (arg == "--speed" && hasNext) { a.speed      = std::stof (argv[++i]); }
        else if (arg == "--block" && hasNext) { a.block      = std::stoi (argv[++i]); }
        else if (arg == "--intent-mode" && hasNext)
        {
            juce::String m (argv[++i]);
            if      (m == "v2_shadow") a.intentMode = IntentMode::V2_SHADOW;
            else if (m == "v2_active") a.intentMode = IntentMode::V2_ACTIVE;
            else                       a.intentMode = IntentMode::PRODUCTION;
        }
        else if (arg == "--smoke") { a.smoke = true; }
    }

    if (! a.inputWav.existsAsFile())
    { std::fprintf (stderr, "FAIL: input not found: %s\n", a.inputWav.getFullPathName().toRawUTF8()); return a; }
    if (! a.smoke && ! a.traceJson.getFullPathName().isNotEmpty())
    { std::fprintf (stderr, "FAIL: --trace <path> required (or use --smoke)\n"); return a; }

    a.valid = true;
    return a;
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    const Args a = parseArgs (argc, argv);
    if (! a.valid) return 1;

    // --- Load input WAV ---
    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wavFmt.createReaderFor (new juce::FileInputStream (a.inputWav), true));
    if (! reader)
    {
        std::fprintf (stderr, "FAIL: cannot read %s\n", a.inputWav.getFullPathName().toRawUTF8());
        return 1;
    }
    const double sr     = reader->sampleRate;
    const int    srcCh  = (int) reader->numChannels;
    const int    tot    = (int) reader->lengthInSamples;
    constexpr int nch   = 2;   // plugin is always stereo

    juce::AudioBuffer<float> monoIn (srcCh, tot);
    reader->read (&monoIn, 0, tot, 0, true, srcCh > 1);
    reader.reset();

    // Duplicate mono → stereo so processBlock always gets a 2-channel buffer
    juce::AudioBuffer<float> inBuf (nch, tot);
    for (int c = 0; c < nch; ++c)
        inBuf.copyFrom (c, 0, monoIn, std::min (c, srcCh - 1), 0, tot);

    // --- Set up processor ---
    SattariAutoPitchAudioProcessor proc;
    auto& apvts = proc.parameters();

    setParam (apvts, "keyRoot",          (float) rootStringToIndex (a.key));
    setParam (apvts, "scale",            a.scale == "minor" ? 1.0f
                                       : a.scale == "chromatic" ? 2.0f : 0.0f);
    setParam (apvts, "amount",           a.amount);
    setParam (apvts, "speed",            a.speed);
    setParam (apvts, "enableCorrection", 1.0f);
    setParam (apvts, "doNoHarm",         0.0f);
    setParam (apvts, "formantPreserve",  1.0f);
    setParam (apvts, "classicMode",      0.0f);
    setParam (apvts, "flexTune",         0.0f);
    setParam (apvts, "transition",       0.5f);
    setParam (apvts, "throat",           1.0f);
    setParam (apvts, "humanize",         0.0f);

    proc.setIntentMode (a.intentMode);
    proc.prepareToPlay (sr, a.block);

    // --- Render block by block ---
    juce::AudioBuffer<float> outBuf (nch, tot);
    outBuf.clear();

    // Trace state
    juce::var framesVar;
    int blockIdx = 0;
    double timeSec = 0.0;

    for (int start = 0; start < tot; start += a.block)
    {
        const int n = std::min (a.block, tot - start);
        juce::AudioBuffer<float> block (nch, n);
        for (int c = 0; c < nch; ++c)
            block.copyFrom (c, 0, inBuf, c, start, n);

        juce::MidiBuffer midi;
        proc.processBlock (block, midi);

        for (int c = 0; c < nch; ++c)
            outBuf.copyFrom (c, start, block, c, 0, n);

        // Capture frame snapshot
        const auto snap = proc.getLiveSnapshot();
        const auto& is  = proc.getIntentV2State();

        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty ("blockIndex",      blockIdx);
        obj->setProperty ("timeSec",         timeSec);
        obj->setProperty ("voiced",          snap.voiced);
        obj->setProperty ("detectedMidi",    snap.voiced ? snap.detectedMidi : -1);
        obj->setProperty ("correctionCents", (double) snap.correctionCents);
        obj->setProperty ("confidence",      (double) snap.confidence);
        obj->setProperty ("detectedRoot",    snap.detectedRoot);
        obj->setProperty ("intentTarget",    is.v2Target);
        obj->setProperty ("intentConf",      (double) is.intentConf);
        obj->setProperty ("intentReason",    (int) is.reason);
        obj->setProperty ("intentReasonName",juce::String (reasonName (is.reason)));
        obj->setProperty ("intentFallback",  is.fallback);
        obj->setProperty ("prodTarget",      is.prodTarget);
        // Phase 3 — continuous motion estimator output
        obj->setProperty ("motionMidiFloat",    is.lastMotion.midiFloat);
        obj->setProperty ("motionVelMidi",      is.lastMotion.velocityMidiPerSec);
        obj->setProperty ("motionVelCents",     is.lastMotion.velocityCentsPerSec);
        obj->setProperty ("motionDir",          is.lastMotion.direction);
        obj->setProperty ("motionConf",         is.lastMotion.confidence);
        obj->setProperty ("motionAccel",        is.lastMotion.acceleration);
        obj->setProperty ("motionIsVibrato",    is.lastMotion.isVibrato);
        framesVar.append (juce::var (obj.get()));

        ++blockIdx;
        timeSec += (double) n / sr;
    }

    // --- Smoke check ---
    if (a.smoke)
    {
        bool finite = true;
        for (int c = 0; c < outBuf.getNumChannels() && finite; ++c)
            for (int i = 0; i < outBuf.getNumSamples(); ++i)
                if (! std::isfinite (outBuf.getSample (c, i))) { finite = false; break; }
        float rms = 0.0f;
        for (int c = 0; c < outBuf.getNumChannels(); ++c)
            rms += outBuf.getRMSLevel (c, 0, outBuf.getNumSamples());
        rms /= (float) outBuf.getNumChannels();
        if (! finite) { std::fprintf (stderr, "FAIL: non-finite samples\n"); return 1; }
        if (rms < 1.0e-4f) { std::fprintf (stderr, "FAIL: silent output (rms %.6f)\n", rms); return 1; }
        std::printf ("smoke ok (rms=%.4f, blocks=%d)\n", rms, blockIdx);
        return 0;
    }

    // --- Write output WAV ---
    if (a.outputWav.getFullPathName().isNotEmpty())
    {
        a.outputWav.getParentDirectory().createDirectory();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (
                new juce::FileOutputStream (a.outputWav), sr, (unsigned) nch, 16, {}, 0));
        if (writer)
            writer->writeFromAudioSampleBuffer (outBuf, 0, tot);
    }

    // --- Write trace JSON ---
    juce::DynamicObject::Ptr rootObj = new juce::DynamicObject();
    rootObj->setProperty ("frames",     framesVar);
    rootObj->setProperty ("inputFile",  a.inputWav.getFullPathName());
    rootObj->setProperty ("key",        a.key);
    rootObj->setProperty ("scale",      a.scale);
    rootObj->setProperty ("amount",     (double) a.amount);
    rootObj->setProperty ("speed",      (double) a.speed);
    rootObj->setProperty ("intentMode", (int) a.intentMode);
    rootObj->setProperty ("sampleRate", sr);
    rootObj->setProperty ("blockSize",  a.block);

    const juce::String traceStr = juce::JSON::toString (juce::var (rootObj.get()), false);
    a.traceJson.getParentDirectory().createDirectory();
    a.traceJson.replaceWithText (traceStr);

    std::printf ("traced %d blocks (%.2fs) -> %s\n",
                 blockIdx, timeSec, a.traceJson.getFullPathName().toRawUTF8());
    return 0;
}
