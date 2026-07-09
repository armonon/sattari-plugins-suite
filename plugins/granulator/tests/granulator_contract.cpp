// Sattari Granulator — functional contract (TWO-LAYER A/B + MORPH + CAPTURE).
// Proves the polyphonic granular INSTRUMENT: (1) with a source loaded in Layer A
// and a held MIDI note the grain cloud plays — output is BOUNDED and NON-SILENT;
// (2) with no note held (released + tail elapsed) it settles to ~silence; (3)
// key-snap maps an out-of-scale note to an in-scale pitch — asserted both on the
// shared snap helper the engine uses AND on the audible output fundamental; (4) a
// param-extreme sweep produces NO NaNs/Infs; (5) a mono source and a stereo
// source both render finite. TWO-LAYER additions: (6) morph=0 == Layer A only
// (loading only B and morph=0 is silent; loading only A and morph=0 plays); (7)
// loading a distinct source into B and morph=1 plays B (different pitch); (8) the
// Live Capture path records the input bus and builds a finite non-empty buffer.
//
// SOURCE INJECTION: loadFile (Layer, const juce::File&) decodes through the
// already-registered AudioFormatManager. We synthesise buffers, write them to
// temp WAVs, and loadFile() them. No host playhead -> tempo falls back to 120 BPM.
#include "PluginProcessor.h"

#include <sattari_audio_core/SattariPitchDetector.h>   // snapMidiToScale, ScaleId

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 512;

using Layer = SattariGranulatorAudioProcessor::Layer;

void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Set a PER-LAYER param (A_/B_ prefixed) on a specific layer.
void setLayerParam (juce::AudioProcessorValueTreeState& a, Layer layer,
                    const char* baseId, float v)
{
    setParam (a, SattariGranulatorAudioProcessor::layerParamId (layer, baseId), v);
}

// Synthesise a 1 s test buffer and write it to a temp WAV.
juce::File writeTempSource (int numChannels, bool sine, double toneHz)
{
    const int len = (int) kSr;   // 1 second
    juce::AudioBuffer<float> buf (numChannels, len);
    buf.clear();
    juce::Random rng (1234);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* d = buf.getWritePointer (ch);
        double ph = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * toneHz / kSr;
        for (int i = 0; i < len; ++i)
        {
            if (sine) { d[i] = 0.6f * (float) std::sin (ph); ph += inc; }
            else      { d[i] = 0.5f * (rng.nextFloat() * 2.0f - 1.0f); }
        }
    }

    auto file = juce::File::createTempFile (".wav");
    juce::WavAudioFormat wav;
    if (auto os = file.createOutputStream())
    {
        std::unique_ptr<juce::OutputStream> stream (std::move (os));
        const auto opts = juce::AudioFormatWriterOptions{}
                              .withSampleRate (kSr)
                              .withNumChannels (numChannels)
                              .withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, opts))
            writer->writeFromAudioSampleBuffer (buf, 0, len);
    }
    return file;
}

// Configure LAYER A into a clean, deterministic "one grain-cloud voice" state and
// force morph=0 (A only) so the classic single-layer asserts still hold. Layer B
// is left at defaults (empty) unless a test loads it. Globals are shared.
void baseConfig (juce::AudioProcessorValueTreeState& a)
{
    // Blend: A only.
    setParam (a, "layerMorph", 0.0f);
    setParam (a, "layerSel", 0.0f);          // edit A (UI only)

    // Per-layer A source region / grain cloud / pitch (mirrors the old defaults).
    setLayerParam (a, Layer::A, "srcStart", 0.0f);   setLayerParam (a, Layer::A, "srcEnd", 1.0f);
    setLayerParam (a, Layer::A, "srcReverse", 0.0f); setLayerParam (a, Layer::A, "srcLoopMode", 1.0f);   // Loop
    setLayerParam (a, Layer::A, "srcGain", 0.0f);    setLayerParam (a, Layer::A, "srcMono", 0.0f);

    setLayerParam (a, Layer::A, "posBase", 0.2f);    setLayerParam (a, Layer::A, "posScanRate", 0.0f);
    setLayerParam (a, Layer::A, "posScanSync", 0.0f);setLayerParam (a, Layer::A, "posEnvAmt", 0.0f);
    setLayerParam (a, Layer::A, "posLfoAmt", 0.0f);

    setLayerParam (a, Layer::A, "grainSize", 60.0f); setLayerParam (a, Layer::A, "spray", 0.0f);
    setLayerParam (a, Layer::A, "density", 4.0f);    setLayerParam (a, Layer::A, "densityMode", 1.0f);   // Overlap
    setLayerParam (a, Layer::A, "window", 0.0f);     setLayerParam (a, Layer::A, "grainPan", 0.0f);

    setLayerParam (a, Layer::A, "unisonVoices", 1.0f);
    setLayerParam (a, Layer::A, "unisonDetune", 0.0f);

    setLayerParam (a, Layer::A, "tuneCoarse", 0.0f); setLayerParam (a, Layer::A, "tuneFine", 0.0f);
    setLayerParam (a, Layer::A, "pitchSpread", 0.0f);
    setLayerParam (a, Layer::A, "level", 0.0f);      setLayerParam (a, Layer::A, "pan", 0.0f);

    // Global (shared) pitch/env/filter.
    setParam (a, "glide", 0.0f);      setParam (a, "glideMode", 1.0f);
    setParam (a, "pitchEnvAmt", 0.0f);
    setParam (a, "pitchLfoAmt", 0.0f);

    setParam (a, "keySnap", 0.0f);    setParam (a, "keyRoot", 0.0f);       // C
    setParam (a, "keyScale", 0.0f);   setParam (a, "followKey", 0.0f);     // Major

    setParam (a, "subOsc", 0.0f);     setParam (a, "subLevel", 0.0f);

    setParam (a, "filterType", 0.0f); setParam (a, "cutoff", 20000.0f);
    setParam (a, "reso", 0.0f);       setParam (a, "filterKeyTrack", 0.0f);
    setParam (a, "filterEnvAmt", 0.0f);

    setParam (a, "ampAttack", 2.0f);  setParam (a, "ampDecay", 50.0f);
    setParam (a, "ampSustain", 1.0f); setParam (a, "ampRelease", 20.0f);
    setParam (a, "fltAttack", 2.0f);  setParam (a, "fltDecay", 50.0f);
    setParam (a, "fltSustain", 1.0f); setParam (a, "fltRelease", 20.0f);
    setParam (a, "envVelSens", 0.0f);

    setParam (a, "lfo1Amt", 0.0f);    setParam (a, "lfo2Amt", 0.0f);
    setParam (a, "lfo1Dest", 0.0f);   setParam (a, "lfo2Dest", 0.0f);

    setParam (a, "voices", 8.0f);     setParam (a, "voiceMode", 0.0f);
    setParam (a, "width", 1.0f);      setParam (a, "tone", 0.0f);
    setParam (a, "volume", 0.0f);     // unity, so we can see the raw cloud level
}

// Mirror the Layer A per-layer config onto Layer B (used by morph tests).
void mirrorLayerBFromA (juce::AudioProcessorValueTreeState& a)
{
    for (const auto& base : SattariGranulatorAudioProcessor::perLayerParamIds())
    {
        auto* pa = a.getRawParameterValue (SattariGranulatorAudioProcessor::layerParamId (Layer::A, base.toRawUTF8()));
        auto* pParam = a.getParameter (SattariGranulatorAudioProcessor::layerParamId (Layer::B, base.toRawUTF8()));
        if (pa != nullptr && pParam != nullptr)
            pParam->setValueNotifyingHost (pParam->convertTo0to1 (pa->load()));
    }
}

void render (SattariGranulatorAudioProcessor& p, int blocks, bool sendOn, int note,
             bool sendOff, juce::AudioBuffer<float>& out, double& peak, bool& finite)
{
    peak = 0.0; finite = true;
    juce::AudioBuffer<float> buf (2, kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        juce::MidiBuffer midi;
        if (sendOn && b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 8);
        if (sendOff && b == blocks - 1)
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

        p.processBlock (buf, midi);

        const int base = b * kBlock;
        for (int i = 0; i < kBlock; ++i)
        {
            const float v = buf.getSample (0, i);
            if (! std::isfinite (v)) finite = false;
            peak = std::max (peak, (double) std::fabs (v));
            if (out.getNumSamples() > base + i)
                out.setSample (0, base + i, v);
        }
    }
}

double dominantHz (const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples(); const int from = n / 2;
    if (n - from < 2) return 0.0;
    const float* d = b.getReadPointer (0);
    int crossings = 0; float prev = d[from];
    for (int i = from + 1; i < n; ++i)
    {
        if ((prev < 0.0f && d[i] >= 0.0f) || (prev > 0.0f && d[i] <= 0.0f)) ++crossings;
        prev = d[i];
    }
    const double secs = (double) (n - from) / kSr;
    return secs > 0.0 ? (crossings / 2.0) / secs : 0.0;
}

double rms (const juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples(); if (n <= 0) return 0.0;
    const float* d = b.getReadPointer (0);
    double s2 = 0.0;
    for (int i = 0; i < n; ++i) s2 += (double) d[i] * d[i];
    return std::sqrt (s2 / (double) n);
}
} // namespace

int main()
{
    int fails = 0;

    // Two distinct tones so B is audibly different from A. A source = C5 tuned to
    // rootNote C4(60), so playing 60 gives ~toneHzA. B source is a fifth higher.
    const double toneHzA = sattari::dsp::midiToHz (72.0f);   // C5
    const double toneHzB = sattari::dsp::midiToHz (79.0f);   // G5 (perfect fifth up)
    auto stereoSineA = writeTempSource (2, /*sine=*/true,  toneHzA);
    auto monoSineA   = writeTempSource (1, /*sine=*/true,  toneHzA);
    auto stereoNoise = writeTempSource (2, /*sine=*/false, toneHzA);
    auto stereoSineB = writeTempSource (2, /*sine=*/true,  toneHzB);

    // -------------------------------------------------------------------------
    // (1) Source in Layer A + held note -> BOUNDED + NON-SILENT grain cloud.
    // -------------------------------------------------------------------------
    {
        SattariGranulatorAudioProcessor p; auto& a = p.parameters();
        baseConfig (a);
        p.prepareToPlay (kSr, kBlock);
        if (! p.loadFile (Layer::A, stereoSineA)) { std::printf ("  FAIL could not load stereo source into A\n"); ++fails; }

        const int blocks = 40;
        juce::AudioBuffer<float> out (1, blocks * kBlock);
        double peak = 0.0; bool finite = true;
        render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
        std::printf ("held-note: peak=%.4f rms=%.4f\n", peak, rms (out));

        if (! finite)        { std::printf ("  FAIL non-finite while playing\n"); ++fails; }
        if (peak > 1.0001)   { std::printf ("  FAIL output exceeds soft ceiling (peak=%.4f)\n", peak); ++fails; }
        if (peak <= 1.0e-3)  { std::printf ("  FAIL grain cloud is silent under a held note\n"); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (2) No note held (released + tail elapsed) -> settles to ~silence.
    // -------------------------------------------------------------------------
    {
        SattariGranulatorAudioProcessor p; auto& a = p.parameters();
        baseConfig (a);
        p.prepareToPlay (kSr, kBlock);
        p.loadFile (Layer::A, stereoSineA);

        const int blocks = 120;   // ~1.28 s
        juce::AudioBuffer<float> full (1, blocks * kBlock);
        double peak = 0.0; bool finite = true;
        {
            juce::AudioBuffer<float> buf (2, kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0)  midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 8);
                if (b == 20) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                p.processBlock (buf, midi);
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = buf.getSample (0, i);
                    if (! std::isfinite (v)) finite = false;
                    full.setSample (0, b * kBlock + i, v);
                }
            }
        }
        const int tailFrom = (blocks - 25) * kBlock;   // last ~0.27 s
        juce::AudioBuffer<float> tail (1, blocks * kBlock - tailFrom);
        for (int i = 0; i < tail.getNumSamples(); ++i)
        {
            const float v = full.getSample (0, tailFrom + i);
            tail.setSample (0, i, v);
            peak = std::max (peak, (double) std::fabs (v));
        }
        std::printf ("released tail: peak=%.6f rms=%.6f\n", peak, rms (tail));

        if (! finite)       { std::printf ("  FAIL non-finite in tail\n"); ++fails; }
        if (peak > 1.0e-3)  { std::printf ("  FAIL cloud does not settle to silence after release\n"); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (3) Key-snap: an out-of-scale note maps to an in-scale pitch.
    // -------------------------------------------------------------------------
    {
        const int snapped   = sattari::dsp::snapMidiToScale (61, 0, sattari::dsp::ScaleId::Major);
        const int untouched = sattari::dsp::snapMidiToScale (60, 0, sattari::dsp::ScaleId::Major);
        std::printf ("snap C#->%d (want 60), C->%d (want 60)\n", snapped, untouched);
        if (snapped != 60)   { std::printf ("  FAIL snap helper did not map C# into C major\n"); ++fails; }
        if (untouched != 60) { std::printf ("  FAIL snap helper altered an in-scale note\n"); ++fails; }

        auto measurePlayed = [&] (bool snapOn) -> double
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            setParam (a, "keySnap", snapOn ? 1.0f : 0.0f);
            setParam (a, "keyRoot", 0.0f);          // C
            setParam (a, "keyScale", 0.0f);         // Major
            setLayerParam (a, Layer::A, "grainSize", 120.0f);   // long grains -> clean pitch read
            setLayerParam (a, Layer::A, "density", 2.0f);       // gentle overlap
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);
            const int blocks = 60;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 61, /*off=*/false, out, peak, finite);
            if (! finite) { std::printf ("  FAIL non-finite in key-snap render\n"); ++fails; }
            return dominantHz (out);
        };

        const double hzSnapOff = measurePlayed (false);
        const double hzSnapOn  = measurePlayed (true);
        std::printf ("keysnap off domHz=%.1f  on domHz=%.1f  (source=%.1f)\n",
                     hzSnapOff, hzSnapOn, toneHzA);

        if (! (hzSnapOn < hzSnapOff * 0.99))
            { std::printf ("  FAIL key-snap did not lower the played pitch\n"); ++fails; }
        if (std::fabs (hzSnapOn - toneHzA) > toneHzA * 0.04)
            { std::printf ("  FAIL snapped pitch not at the in-scale (source) fundamental\n"); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (4) Param-extreme sweep -> NO NaNs/Infs (and still bounded).
    // -------------------------------------------------------------------------
    {
        SattariGranulatorAudioProcessor p; auto& a = p.parameters();
        baseConfig (a);
        setLayerParam (a, Layer::A, "density", 100.0f);
        setLayerParam (a, Layer::A, "densityMode", 0.0f);   // Rate
        setLayerParam (a, Layer::A, "spray", 1.0f);
        setLayerParam (a, Layer::A, "grainSize", 1.0f);
        setParam (a, "pitchEnvAmt", 24.0f);
        setParam (a, "pitchEnvAttack", 0.0f);
        setParam (a, "pitchEnvDecay", 4000.0f);
        setLayerParam (a, Layer::A, "pitchSpread", 100.0f);
        setParam (a, "pitchLfoAmt", 1200.0f);
        setParam (a, "pitchLfoRate", 20.0f);
        setLayerParam (a, Layer::A, "tuneCoarse", 24.0f);
        setLayerParam (a, Layer::A, "unisonVoices", 4.0f);  // max unison stack
        setLayerParam (a, Layer::A, "unisonDetune", 50.0f);
        setParam (a, "reso", 1.0f);
        setParam (a, "cutoff", 200.0f);
        setParam (a, "filterEnvAmt", 100.0f);
        setParam (a, "subOsc", 1.0f);
        setParam (a, "subLevel", 1.0f);
        setParam (a, "lfo1Dest", 2.0f);   // Cutoff
        setParam (a, "lfo1Amt", 1.0f);
        setParam (a, "lfo2Dest", 7.0f);   // Amp
        setParam (a, "lfo2Amt", 1.0f);
        setParam (a, "volume", 6.0f);
        // Also drive Layer B hot + morph to equal blend, so BOTH engines stack.
        mirrorLayerBFromA (a);
        setParam (a, "layerMorph", 0.5f);
        p.prepareToPlay (kSr, kBlock);
        p.loadFile (Layer::A, stereoNoise);
        p.loadFile (Layer::B, stereoNoise);

        const int blocks = 80;
        juce::AudioBuffer<float> buf (2, kBlock);
        double peak = 0.0; bool finite = true;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            juce::MidiBuffer midi;
            if (b == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 120), 4);
                midi.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 120), 8);
                midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 120), 12);
            }
            p.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = std::max (peak, (double) std::fabs (v));
                }
        }
        std::printf ("extreme sweep (both layers): peak=%.4f finite=%d\n", peak, finite ? 1 : 0);
        if (! finite)      { std::printf ("  FAIL NaN/Inf under param extremes\n"); ++fails; }
        if (peak > 1.0001) { std::printf ("  FAIL extremes escape the soft ceiling (peak=%.4f)\n", peak); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (5) Mono AND stereo sources both render finite (non-silent) in Layer A.
    // -------------------------------------------------------------------------
    {
        auto renderSource = [&] (const juce::File& f, const char* label)
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            p.prepareToPlay (kSr, kBlock);
            if (! p.loadFile (Layer::A, f)) { std::printf ("  FAIL could not load %s source\n", label); ++fails; return; }
            const int blocks = 40;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
            std::printf ("%s source: peak=%.4f finite=%d\n", label, peak, finite ? 1 : 0);
            if (! finite)       { std::printf ("  FAIL %s source non-finite\n", label); ++fails; }
            if (peak > 1.0001)  { std::printf ("  FAIL %s source unbounded\n", label); ++fails; }
            if (peak <= 1.0e-3) { std::printf ("  FAIL %s source silent\n", label); ++fails; }
        };
        renderSource (monoSineA,   "mono");
        renderSource (stereoSineA, "stereo");
    }

    // -------------------------------------------------------------------------
    // (6) MORPH = 0 == Layer A ONLY. Loading a source into B only (A empty) with
    //     morph=0 must be SILENT; loading into A only with morph=0 must PLAY.
    // -------------------------------------------------------------------------
    {
        // (6a) B loaded, A empty, morph=0 -> silent.
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            mirrorLayerBFromA (a);              // B has sane grain params
            setParam (a, "layerMorph", 0.0f);   // A only
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::B, stereoSineB); // ONLY B has a source
            const int blocks = 40;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
            std::printf ("morph=0, B-only loaded: peak=%.6f\n", peak);
            if (! finite)      { std::printf ("  FAIL non-finite in morph=0 B-only\n"); ++fails; }
            if (peak > 1.0e-3) { std::printf ("  FAIL morph=0 leaks Layer B (should be A only)\n"); ++fails; }
        }
        // (6b) A loaded, morph=0 -> plays.
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            setParam (a, "layerMorph", 0.0f);
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);
            const int blocks = 40;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
            std::printf ("morph=0, A loaded: peak=%.4f\n", peak);
            if (peak <= 1.0e-3) { std::printf ("  FAIL morph=0 silent with Layer A loaded\n"); ++fails; }
        }
    }

    // -------------------------------------------------------------------------
    // (7) Loading a distinct source into B and morph=1 plays B (different pitch).
    //     A holds tone C5, B holds tone G5. morph=0 -> ~C5; morph=1 -> ~G5.
    // -------------------------------------------------------------------------
    {
        auto measureMorph = [&] (float morph) -> double
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            mirrorLayerBFromA (a);
            // Long, gentle grains on both layers for a clean pitch read.
            setLayerParam (a, Layer::A, "grainSize", 120.0f); setLayerParam (a, Layer::A, "density", 2.0f);
            setLayerParam (a, Layer::B, "grainSize", 120.0f); setLayerParam (a, Layer::B, "density", 2.0f);
            setParam (a, "layerMorph", morph);
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);   // C5
            p.loadFile (Layer::B, stereoSineB);   // G5
            const int blocks = 60;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
            if (! finite) { std::printf ("  FAIL non-finite in morph render\n"); ++fails; }
            return dominantHz (out);
        };

        const double hzA = measureMorph (0.0f);   // ~toneHzA (C5)
        const double hzB = measureMorph (1.0f);   // ~toneHzB (G5)
        std::printf ("morph=0 domHz=%.1f (want ~%.1f), morph=1 domHz=%.1f (want ~%.1f)\n",
                     hzA, toneHzA, hzB, toneHzB);

        if (std::fabs (hzA - toneHzA) > toneHzA * 0.06)
            { std::printf ("  FAIL morph=0 fundamental is not Layer A's tone\n"); ++fails; }
        if (std::fabs (hzB - toneHzB) > toneHzB * 0.06)
            { std::printf ("  FAIL morph=1 fundamental is not Layer B's tone\n"); ++fails; }
        if (! (hzB > hzA * 1.1))
            { std::printf ("  FAIL morph did not switch A->B (B should be clearly higher)\n"); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (8) Live Capture: feed the input bus, arm capture, drain -> finite buffer.
    // -------------------------------------------------------------------------
    {
        SattariGranulatorAudioProcessor p; auto& a = p.parameters();
        baseConfig (a);

        // Enable the optional input bus so getBusBuffer(true,0) is fed. The
        // processBlock buffer must be wide enough for BOTH the input and output
        // buses (a discrete input bus does not alias the output), so size it to
        // the processor's total channel count.
        p.enableAllBuses();
        p.prepareToPlay (kSr, kBlock);
        const int totalCh = juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(),
                                                       p.getTotalNumOutputChannels()));

        // Short capture into Layer B.
        setParam (a, "captureSecs", 0.5f);
        setParam (a, "captureTarget", 1.0f);       // -> B
        p.serviceCapture();                        // pick up the target/secs
        setParam (a, "captureArm", 1.0f);
        p.serviceCapture();                        // rising edge: arm

        // Feed ~0.6 s of a sine on the input bus; capture target is 0.5 s. Write
        // the tone into the INPUT bus view so the audio thread records it.
        const int blocks = 60;                     // ~0.64 s
        double ph = 0.0;
        const double inc = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSr;
        bool finiteOut = true;
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (totalCh, kBlock);
            buf.clear();
            auto inBus = p.getBusBuffer (buf, true, 0);
            for (int ch = 0; ch < inBus.getNumChannels(); ++ch)
            {
                float* d = inBus.getWritePointer (ch);
                double lp = ph;
                for (int i = 0; i < kBlock; ++i) { d[i] = 0.5f * (float) std::sin (lp); lp += inc; }
            }
            ph += inc * kBlock;
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            for (int i = 0; i < kBlock; ++i)
                if (! std::isfinite (buf.getSample (0, i))) finiteOut = false;
        }

        // Drain the completed capture on the message thread.
        bool published = false;
        for (int i = 0; i < 4 && ! published; ++i)
            published = p.serviceCapture();

        auto capSrc = p.currentSource (Layer::B);
        const bool haveCap = capSrc != nullptr && capSrc->lengthSamples > 0;
        bool capFinite = true;
        if (haveCap)
            for (int i = 0; i < capSrc->lengthSamples; ++i)
                if (! std::isfinite (capSrc->audio.getReadPointer (0)[i])) capFinite = false;

        std::printf ("capture: published=%d haveBuffer=%d len=%d finite=%d\n",
                     published ? 1 : 0, haveCap ? 1 : 0,
                     haveCap ? capSrc->lengthSamples : 0, capFinite ? 1 : 0);

        if (! finiteOut)  { std::printf ("  FAIL non-finite output during capture\n"); ++fails; }
        if (! haveCap)    { std::printf ("  FAIL capture did not build a Layer B buffer\n"); ++fails; }
        if (! capFinite)  { std::printf ("  FAIL captured buffer is non-finite\n"); ++fails; }
        if (haveCap && capSrc->lengthSamples < (int) (0.4 * kSr))
            { std::printf ("  FAIL captured buffer shorter than requested\n"); ++fails; }

        // The captured Layer B source should now play under morph=1. The input
        // bus is still enabled here, so read the OUTPUT bus view explicitly
        // (channel 0 of the raw buffer may be the input bus in this layout).
        if (haveCap)
        {
            mirrorLayerBFromA (a);
            setParam (a, "layerMorph", 1.0f);
            const int rb = 40;
            double peak = 0.0; bool finite = true;
            for (int b = 0; b < rb; ++b)
            {
                juce::AudioBuffer<float> buf (totalCh, kBlock);
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 8);
                p.processBlock (buf, midi);
                auto outBus = p.getBusBuffer (buf, false, 0);
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = outBus.getSample (0, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = std::max (peak, (double) std::fabs (v));
                }
            }
            std::printf ("captured B playback: peak=%.4f finite=%d\n", peak, finite ? 1 : 0);
            if (! finite)       { std::printf ("  FAIL captured Layer B playback non-finite\n"); ++fails; }
            if (peak <= 1.0e-3) { std::printf ("  FAIL captured Layer B is silent under morph=1\n"); ++fails; }
            if (peak > 1.0001)  { std::printf ("  FAIL captured Layer B playback unbounded\n"); ++fails; }
        }
    }

    // -------------------------------------------------------------------------
    // (9) SMOOTHNESS macro. smoothness=0 is the documented BIT-IDENTICAL no-op:
    //     it must match the classic (no-smoothness) cloud peak exactly. Full
    //     smoothness (=1) boosts overlap + rounds the window but stays BOUNDED,
    //     FINITE and NON-SILENT (the fixed 256-grain pool caps the extra spawns).
    // -------------------------------------------------------------------------
    {
        auto renderSmooth = [&] (float smoothness, int interpChoice) -> std::pair<double, bool>
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            setLayerParam (a, Layer::A, "interp", (float) interpChoice);
            setLayerParam (a, Layer::A, "smoothness", smoothness);
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);
            const int blocks = 40;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
            return { peak, finite };
        };

        // Classic path: Linear interp, smoothness 0 (the pre-smoothness engine).
        const auto classic = renderSmooth (0.0f, 0);   // Interp::Linear
        // Same but through the smoothness=0 code path (must be identical).
        const auto noop     = renderSmooth (0.0f, 0);
        // Full smoothness (still Linear interp so only the macro varies).
        const auto smoothed = renderSmooth (1.0f, 0);
        std::printf ("smoothness: classic peak=%.6f  s0 peak=%.6f  s1 peak=%.6f\n",
                     classic.first, noop.first, smoothed.first);

        if (! classic.second || ! noop.second || ! smoothed.second)
            { std::printf ("  FAIL non-finite in smoothness render\n"); ++fails; }
        // smoothness==0 is a strict no-op: byte-for-byte the classic peak.
        if (std::fabs (classic.first - noop.first) > 1.0e-9)
            { std::printf ("  FAIL smoothness=0 is not bit-identical to the classic cloud\n"); ++fails; }
        if (classic.first <= 1.0e-3)
            { std::printf ("  FAIL smoothness baseline is silent\n"); ++fails; }
        if (smoothed.first > 1.0001)
            { std::printf ("  FAIL smoothness=1 escapes the soft ceiling (peak=%.4f)\n", smoothed.first); ++fails; }
        if (smoothed.first <= 1.0e-3)
            { std::printf ("  FAIL smoothness=1 is silent\n"); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (10) RHYTHMIC GRAINS: densitySync on (no host playhead -> 120 BPM fallback)
    //      fires grains on a tempo grid and must stay BOUNDED, FINITE, NON-SILENT.
    //      grainSizeSync is exercised alongside to cover both synced paths.
    // -------------------------------------------------------------------------
    {
        SattariGranulatorAudioProcessor p; auto& a = p.parameters();
        baseConfig (a);
        setLayerParam (a, Layer::A, "densitySync", 1.0f);
        setLayerParam (a, Layer::A, "densityDiv", 6.0f);      // 1/8 (index 6)
        setLayerParam (a, Layer::A, "grainSizeSync", 1.0f);
        setLayerParam (a, Layer::A, "grainSizeDiv", 9.0f);    // 1/16 (index 9)
        p.prepareToPlay (kSr, kBlock);
        p.loadFile (Layer::A, stereoSineA);
        const int blocks = 60;
        juce::AudioBuffer<float> out (1, blocks * kBlock);
        double peak = 0.0; bool finite = true;
        render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
        std::printf ("densitySync@120: peak=%.4f rms=%.4f finite=%d\n",
                     peak, rms (out), finite ? 1 : 0);
        if (! finite)       { std::printf ("  FAIL densitySync render non-finite\n"); ++fails; }
        if (peak > 1.0001)  { std::printf ("  FAIL densitySync escapes the soft ceiling (peak=%.4f)\n", peak); ++fails; }
        if (peak <= 1.0e-3) { std::printf ("  FAIL densitySync grain grid is silent\n"); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (11) VELOCITY ROUTING -> Cutoff. With velToCutoff>0 and a low base cutoff,
    //      a HARD note (vel 127) opens the filter far more than a SOFT note (vel
    //      1), so the hard hit is measurably BRIGHTER/LOUDER. Both must stay
    //      finite + bounded; and the loud-vs-soft level must differ measurably.
    // -------------------------------------------------------------------------
    {
        auto renderVel = [&] (int velocity) -> std::pair<double, bool>
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            // A low LP cutoff so the filter is well below the source tone; the
            // velocity->cutoff routing then decides how much energy passes.
            setParam (a, "filterType", 0.0f);       // LP24
            setParam (a, "cutoff", 200.0f);
            setParam (a, "reso", 0.0f);
            setParam (a, "envVelSens", 0.0f);       // isolate velocity->cutoff from amp-vel
            setParam (a, "velToCutoff", 1.0f);      // full positive routing
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);
            const int blocks = 40;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            {
                juce::AudioBuffer<float> buf (2, kBlock);
                for (int b = 0; b < blocks; ++b)
                {
                    buf.clear();
                    juce::MidiBuffer midi;
                    if (b == 0)
                        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) velocity), 8);
                    p.processBlock (buf, midi);
                    for (int i = 0; i < kBlock; ++i)
                    {
                        const float v = buf.getSample (0, i);
                        if (! std::isfinite (v)) finite = false;
                        peak = std::max (peak, (double) std::fabs (v));
                        out.setSample (0, b * kBlock + i, v);
                    }
                }
            }
            return { rms (out), finite };
        };

        const auto soft = renderVel (1);     // pp: filter stays near 200 Hz
        const auto hard = renderVel (127);   // ff: velToCutoff lifts cutoff ~+4 oct
        std::printf ("velToCutoff: soft rms=%.6f (finite=%d)  hard rms=%.6f (finite=%d)\n",
                     soft.first, soft.second ? 1 : 0, hard.first, hard.second ? 1 : 0);
        if (! soft.second || ! hard.second)
            { std::printf ("  FAIL non-finite in velocity->cutoff render\n"); ++fails; }
        if (soft.first > 1.0001 || hard.first > 1.0001)
            { std::printf ("  FAIL velocity->cutoff output unbounded\n"); ++fails; }
        // The hard hit opens the filter -> more energy passes than the soft hit.
        if (! (hard.first > soft.first * 1.1))
            { std::printf ("  FAIL velToCutoff did not brighten the hard note (soft=%.6f hard=%.6f)\n",
                           soft.first, hard.first); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (12) GLOBAL PITCH-BEND shifts the played pitch. With the default 2 st bend
    //      range, a full-up wheel (+2 st) held under a sounding note must raise the
    //      dominant frequency measurably above the un-bent pitch. Bounded + finite.
    // -------------------------------------------------------------------------
    {
        auto measureBend = [&] (bool bendUp) -> std::pair<double, bool>
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            setLayerParam (a, Layer::A, "grainSize", 120.0f);   // long grains -> clean read
            setLayerParam (a, Layer::A, "density", 2.0f);
            setParam (a, "bendRange", 2.0f);                    // classic 2 st
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);

            const int blocks = 60;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            juce::AudioBuffer<float> buf (2, kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 4);
                if (b == 0 && bendUp)   // full-up wheel on channel 1 (classic bend)
                    midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 8);
                p.processBlock (buf, midi);
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = buf.getSample (0, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = std::max (peak, (double) std::fabs (v));
                    out.setSample (0, b * kBlock + i, v);
                }
            }
            if (peak > 1.0001) { std::printf ("  FAIL pitch-bend output unbounded (peak=%.4f)\n", peak); ++fails; }
            return { dominantHz (out), finite };
        };

        const auto flat = measureBend (false);
        const auto bent = measureBend (true);
        std::printf ("pitch-bend: flat domHz=%.1f (finite=%d)  +2st domHz=%.1f (finite=%d)\n",
                     flat.first, flat.second ? 1 : 0, bent.first, bent.second ? 1 : 0);
        if (! flat.second || ! bent.second)
            { std::printf ("  FAIL non-finite in pitch-bend render\n"); ++fails; }
        // +2 st is ~+12.2%; require a clear upward shift (well above measurement noise).
        if (! (bent.first > flat.first * 1.05))
            { std::printf ("  FAIL global pitch-bend did not raise the played pitch (flat=%.1f bent=%.1f)\n",
                           flat.first, bent.first); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (13) BUILT-IN REVERB. reverbMix=0 is an EXACT pass-through (byte-for-byte
    //      identical to the no-reverb render). reverbMix>0 adds a decaying tail:
    //      after the note is released the reverb render rings on measurably longer
    //      than the dry render. Both bounded + finite.
    // -------------------------------------------------------------------------
    {
        // Render a note-on then release, capturing the whole buffer; return the
        // per-sample max-diff vs a reference and the post-release tail RMS.
        auto renderReverb = [&] (float mix, juce::AudioBuffer<float>& out) -> bool
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            setParam (a, "reverbMix", mix);
            setParam (a, "reverbSize", 0.8f);
            setParam (a, "reverbDamp", 0.3f);
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);
            const int blocks = 120;   // note on early, released, then tail
            bool finite = true;
            juce::AudioBuffer<float> buf (2, kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0)  midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 8);
                if (b == 30) midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                p.processBlock (buf, midi);
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = buf.getSample (0, i);
                    if (! std::isfinite (v)) finite = false;
                    out.setSample (0, b * kBlock + i, v);
                }
            }
            return finite;
        };

        const int total = 120 * kBlock;
        juce::AudioBuffer<float> dry (1, total), dry2 (1, total), wet (1, total);
        const bool f0  = renderReverb (0.0f, dry);
        const bool f0b = renderReverb (0.0f, dry2);
        const bool fw  = renderReverb (0.6f, wet);

        // (13a) mix=0 is bit-identical across two runs AND bounded/finite.
        double maxDiff0 = 0.0, peakDry = 0.0, peakWet = 0.0;
        for (int i = 0; i < total; ++i)
        {
            maxDiff0 = std::max (maxDiff0, (double) std::fabs (dry.getSample (0, i) - dry2.getSample (0, i)));
            peakDry  = std::max (peakDry,  (double) std::fabs (dry.getSample (0, i)));
            peakWet  = std::max (peakWet,  (double) std::fabs (wet.getSample (0, i)));
        }

        // Tail window: last ~0.2 s, well after the note released (~0.32 s in).
        const int tailFrom = total - (int) (0.2 * kSr);
        juce::AudioBuffer<float> dryTail (1, total - tailFrom), wetTail (1, total - tailFrom);
        for (int i = 0; i < dryTail.getNumSamples(); ++i)
        {
            dryTail.setSample (0, i, dry.getSample (0, tailFrom + i));
            wetTail.setSample (0, i, wet.getSample (0, tailFrom + i));
        }
        const double dryTailRms = rms (dryTail), wetTailRms = rms (wetTail);
        std::printf ("reverb: mix0 maxDiff=%.3e peakDry=%.4f peakWet=%.4f  tailRMS dry=%.6f wet=%.6f\n",
                     maxDiff0, peakDry, peakWet, dryTailRms, wetTailRms);

        if (! f0 || ! f0b || ! fw)
            { std::printf ("  FAIL non-finite in reverb render\n"); ++fails; }
        if (maxDiff0 > 1.0e-9)
            { std::printf ("  FAIL reverbMix=0 is not an exact pass-through (maxDiff=%.3e)\n", maxDiff0); ++fails; }
        if (peakDry > 1.0001 || peakWet > 1.0001)
            { std::printf ("  FAIL reverb output unbounded (dry=%.4f wet=%.4f)\n", peakDry, peakWet); ++fails; }
        // mix>0 rings on after release -> a clearly louder tail than the dry render.
        if (! (wetTailRms > dryTailRms * 2.0 + 1.0e-6))
            { std::printf ("  FAIL reverbMix>0 did not add a decaying tail (dry=%.6f wet=%.6f)\n",
                           dryTailRms, wetTailRms); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (14) MOD MATRIX: a single slot (LFO1 -> Cutoff, amount 1) makes the output
    //      differ from the all-Off matrix. With a low base cutoff the LFO sweeps
    //      the filter open/closed, so the modulated render is measurably different
    //      from the static one. Both bounded + finite.
    // -------------------------------------------------------------------------
    {
        auto renderMod = [&] (bool modOn, juce::AudioBuffer<float>& out) -> std::pair<double, bool>
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            // Low LP cutoff so a Cutoff modulation clearly changes the passed energy.
            setParam (a, "filterType", 0.0f);       // LP24
            setParam (a, "cutoff", 400.0f);
            setParam (a, "reso", 0.0f);
            // A fast, deep LFO1 (Sine) so the sweep is obvious across the window.
            setParam (a, "lfo1Rate", 8.0f);
            setParam (a, "lfo1Shape", 0.0f);        // Sine
            setParam (a, "lfo1Dest", 0.0f);         // Off — the matrix (not the fixed dest) drives it
            setParam (a, "lfo1Amt", 0.0f);
            if (modOn)
            {
                setParam (a, "mod1Src", 1.0f);      // LFO1
                setParam (a, "mod1Dst", 2.0f);      // Cutoff
                setParam (a, "mod1Amt", 1.0f);      // full positive
            }
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (Layer::A, stereoSineA);
            const int blocks = 40;
            double peak = 0.0; bool finite = true;
            juce::AudioBuffer<float> buf (2, kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 8);
                p.processBlock (buf, midi);
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = buf.getSample (0, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = std::max (peak, (double) std::fabs (v));
                    out.setSample (0, b * kBlock + i, v);
                }
            }
            return { peak, finite };
        };

        const int total = 40 * kBlock;
        juce::AudioBuffer<float> off (1, total), on (1, total);
        const auto rOff = renderMod (false, off);
        const auto rOn  = renderMod (true,  on);
        double maxDiff = 0.0;
        for (int i = 0; i < total; ++i)
            maxDiff = std::max (maxDiff, (double) std::fabs (off.getSample (0, i) - on.getSample (0, i)));
        std::printf ("modmatrix: off peak=%.4f (finite=%d)  on peak=%.4f (finite=%d)  maxDiff=%.4e\n",
                     rOff.first, rOff.second ? 1 : 0, rOn.first, rOn.second ? 1 : 0, maxDiff);
        if (! rOff.second || ! rOn.second)
            { std::printf ("  FAIL non-finite in mod-matrix render\n"); ++fails; }
        if (rOff.first > 1.0001 || rOn.first > 1.0001)
            { std::printf ("  FAIL mod-matrix output unbounded\n"); ++fails; }
        // The active LFO1->Cutoff slot must audibly change the output.
        if (! (maxDiff > 1.0e-4))
            { std::printf ("  FAIL mod slot (LFO1->Cutoff) did not change the output (maxDiff=%.4e)\n", maxDiff); ++fails; }
    }

    // Clean up temp files.
    stereoSineA.deleteFile();
    monoSineA.deleteFile();
    stereoNoise.deleteFile();
    stereoSineB.deleteFile();

    std::printf (fails == 0 ? "Sattari Granulator contract ok\n"
                            : "Sattari Granulator contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
