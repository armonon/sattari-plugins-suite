// Sattari Granulator — functional contract.
// Proves the polyphonic granular INSTRUMENT: (1) with a source loaded and a held
// MIDI note the grain cloud plays — output is BOUNDED and NON-SILENT; (2) with no
// note held (released + tail elapsed) it settles to ~silence; (3) key-snap maps an
// out-of-scale note to an in-scale pitch — asserted both on the shared snap helper
// the engine uses AND on the audible output fundamental; (4) a param-extreme sweep
// (max density, max spray, tiny grains, huge pitch env) produces NO NaNs/Infs; and
// (5) a mono source and a stereo source both render finite.
//
// SOURCE INJECTION: the processor's only public source entrypoint is
// loadFile (const juce::File&) — it decodes through the already-registered
// AudioFormatManager. So we synthesise a buffer in memory, write it to a temp WAV
// via juce::WavAudioFormat, and loadFile() it. That exercises the real
// message-thread decode -> atomic_store -> engine.setSource publish path. No host
// playhead is attached, so tempo deterministically falls back to 120 BPM.
#include "PluginProcessor.h"

#include <sattari_audio_core/SattariPitchDetector.h>   // snapMidiToScale, ScaleId

#include <cmath>
#include <cstdio>
#include <memory>

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 512;

void setParam (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Synthesise a 1 s test buffer and write it to a temp WAV so the processor can
// decode it through its own AudioFormatManager (the real load path). `sine` picks
// a clean tone (for the pitch assertions) vs. broadband noise.
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
        // writer dtor flushes + closes the stream it took ownership of.
    }
    return file;
}

// Configure the processor into a clean, deterministic "one grain-cloud voice"
// state: no modulation, no filter colour, unity output. Overrides come after.
void baseConfig (juce::AudioProcessorValueTreeState& a)
{
    setParam (a, "srcStart", 0.0f);   setParam (a, "srcEnd", 1.0f);
    setParam (a, "srcReverse", 0.0f); setParam (a, "srcLoopMode", 1.0f);   // Loop (endless cloud)
    setParam (a, "srcGain", 0.0f);    setParam (a, "srcMono", 0.0f);

    setParam (a, "posBase", 0.2f);    setParam (a, "posScanRate", 0.0f);
    setParam (a, "posScanSync", 0.0f); setParam (a, "posEnvAmt", 0.0f);
    setParam (a, "posLfoAmt", 0.0f);

    setParam (a, "grainSize", 60.0f); setParam (a, "spray", 0.0f);
    setParam (a, "density", 4.0f);    setParam (a, "densityMode", 1.0f);   // Overlap
    setParam (a, "window", 0.0f);     setParam (a, "grainPan", 0.0f);

    setParam (a, "tuneCoarse", 0.0f); setParam (a, "tuneFine", 0.0f);
    setParam (a, "pitchSpread", 0.0f);
    setParam (a, "glide", 0.0f);      setParam (a, "glideMode", 1.0f);
    setParam (a, "pitchEnvAmt", 0.0f);
    setParam (a, "pitchLfoAmt", 0.0f);

    setParam (a, "keySnap", 0.0f);    setParam (a, "keyRoot", 0.0f);       // C
    setParam (a, "keyScale", 0.0f);   setParam (a, "followKey", 0.0f);     // Major

    setParam (a, "subOsc", 0.0f);     setParam (a, "subLevel", 0.0f);

    setParam (a, "filterType", 0.0f); setParam (a, "cutoff", 20000.0f);
    setParam (a, "reso", 0.0f);       setParam (a, "filterKeyTrack", 0.0f);
    setParam (a, "filterEnvAmt", 0.0f);

    // Fast attack, full sustain, short-ish release so the tail clears quickly.
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

// Render `blocks` blocks. When `noteOn` is set, a note-on for `note` is injected in
// the first block; when `noteOff` is set, an all-notes-off is injected in the LAST
// block (so releases + tails happen inside the render). Accumulates ch0 into `out`
// (which must be sized blocks*kBlock), and reports global peak + finiteness.
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

// Dominant frequency of the second half (settled) of ch0 via zero-crossings.
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

    // Build the two test sources once (1 s each). The sine tone is C5 (~523.25 Hz):
    // with the sample rootNote = C4 (60), playing C4 gives ratio 1.0 -> ~523 Hz out.
    const double toneHz = sattari::dsp::midiToHz (72.0f);   // C5
    auto stereoSine = writeTempSource (2, /*sine=*/true,  toneHz);
    auto monoSine   = writeTempSource (1, /*sine=*/true,  toneHz);
    auto stereoNoise = writeTempSource (2, /*sine=*/false, toneHz);

    // -------------------------------------------------------------------------
    // (1) Source loaded + held note -> BOUNDED + NON-SILENT grain cloud.
    // -------------------------------------------------------------------------
    {
        SattariGranulatorAudioProcessor p; auto& a = p.parameters();
        baseConfig (a);
        p.prepareToPlay (kSr, kBlock);
        if (! p.loadFile (stereoSine)) { std::printf ("  FAIL could not load stereo source\n"); ++fails; }

        const int blocks = 40;   // ~0.43 s, plenty for the cloud to fill
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
        p.loadFile (stereoSine);

        // Hold briefly, then release; keep rendering long enough for the 20 ms
        // release + the last grains' windows (<=60 ms) to fully die out.
        const int blocks = 120;   // ~1.28 s
        juce::AudioBuffer<float> full (1, blocks * kBlock);
        double peak = 0.0; bool finite = true;
        // note-on in block 0, all-notes-off in block 20; the remaining ~1 s is tail.
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
        // Measure the LAST quarter-second only (long after release).
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
    //     Assert the shared snap helper the engine uses AND the audible pitch.
    // -------------------------------------------------------------------------
    {
        // (3a) Direct assertion on the exact helper startVoice() calls: C# (61) in
        //      C Major snaps to C (60); a diatonic note is left untouched.
        const int snapped   = sattari::dsp::snapMidiToScale (61, 0, sattari::dsp::ScaleId::Major);
        const int untouched = sattari::dsp::snapMidiToScale (60, 0, sattari::dsp::ScaleId::Major);
        std::printf ("snap C#->%d (want 60), C->%d (want 60)\n", snapped, untouched);
        if (snapped != 60)   { std::printf ("  FAIL snap helper did not map C# into C major\n"); ++fails; }
        if (untouched != 60) { std::printf ("  FAIL snap helper altered an in-scale note\n"); ++fails; }

        // (3b) Audible proof: source is C5 tuned to rootNote C4(60). Playing the
        //      out-of-scale note 61 with keySnap OFF plays at ratio 2^(1/12) (a
        //      C#5-ish pitch); with keySnap ON in C major it snaps to 60 -> ratio
        //      1.0 -> the source pitch. So snap-on's fundamental must be LOWER than
        //      snap-off's, and land near the source tone.
        auto measurePlayed = [&] (bool snapOn) -> double
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            setParam (a, "keySnap", snapOn ? 1.0f : 0.0f);
            setParam (a, "keyRoot", 0.0f);          // C
            setParam (a, "keyScale", 0.0f);         // Major
            setParam (a, "grainSize", 120.0f);      // long grains -> clean pitch read
            setParam (a, "density", 2.0f);          // gentle overlap, low modulation
            p.prepareToPlay (kSr, kBlock);
            p.loadFile (stereoSine);
            const int blocks = 60;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 61, /*off=*/false, out, peak, finite);
            if (! finite) { std::printf ("  FAIL non-finite in key-snap render\n"); ++fails; }
            return dominantHz (out);
        };

        const double hzSnapOff = measurePlayed (false);   // ~ toneHz * 2^(1/12)
        const double hzSnapOn  = measurePlayed (true);    // ~ toneHz (snapped to C)
        std::printf ("keysnap off domHz=%.1f  on domHz=%.1f  (source=%.1f)\n",
                     hzSnapOff, hzSnapOn, toneHz);

        // snap-on must be clearly lower than snap-off (one semitone down) and within
        // a semitone of the source tone; snap-off must be a semitone above it.
        if (! (hzSnapOn < hzSnapOff * 0.99))
            { std::printf ("  FAIL key-snap did not lower the played pitch\n"); ++fails; }
        if (std::fabs (hzSnapOn - toneHz) > toneHz * 0.04)
            { std::printf ("  FAIL snapped pitch not at the in-scale (source) fundamental\n"); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (4) Param-extreme sweep -> NO NaNs/Infs (and still bounded).
    // -------------------------------------------------------------------------
    {
        SattariGranulatorAudioProcessor p; auto& a = p.parameters();
        baseConfig (a);
        setParam (a, "density", 100.0f);        // max grains/overlap
        setParam (a, "densityMode", 0.0f);      // Rate (100 grains/s) then also test Overlap below
        setParam (a, "spray", 1.0f);            // max spray
        setParam (a, "grainSize", 1.0f);        // tiny grains
        setParam (a, "pitchEnvAmt", 24.0f);     // huge pitch env
        setParam (a, "pitchEnvAttack", 0.0f);
        setParam (a, "pitchEnvDecay", 4000.0f);
        setParam (a, "pitchSpread", 100.0f);    // max detune spread
        setParam (a, "pitchLfoAmt", 1200.0f);   // max vibrato
        setParam (a, "pitchLfoRate", 20.0f);
        setParam (a, "tuneCoarse", 24.0f);      // +2 oct
        setParam (a, "reso", 1.0f);             // max resonance
        setParam (a, "cutoff", 200.0f);         // low, resonant
        setParam (a, "filterEnvAmt", 100.0f);
        setParam (a, "subOsc", 1.0f);           // sub engaged
        setParam (a, "subLevel", 1.0f);
        setParam (a, "lfo1Dest", 2.0f);         // Cutoff
        setParam (a, "lfo1Amt", 1.0f);
        setParam (a, "lfo2Dest", 7.0f);         // Amp
        setParam (a, "lfo2Amt", 1.0f);
        setParam (a, "volume", 6.0f);           // hottest output
        p.prepareToPlay (kSr, kBlock);
        p.loadFile (stereoNoise);               // broadband material stresses the read

        // Hold a chord (three notes) to fill many voices + the grain pool.
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
        std::printf ("extreme sweep: peak=%.4f finite=%d\n", peak, finite ? 1 : 0);
        if (! finite)      { std::printf ("  FAIL NaN/Inf under param extremes\n"); ++fails; }
        if (peak > 1.0001) { std::printf ("  FAIL extremes escape the soft ceiling (peak=%.4f)\n", peak); ++fails; }
    }

    // -------------------------------------------------------------------------
    // (5) Mono AND stereo sources both render finite (non-silent).
    // -------------------------------------------------------------------------
    {
        auto renderSource = [&] (const juce::File& f, const char* label)
        {
            SattariGranulatorAudioProcessor p; auto& a = p.parameters();
            baseConfig (a);
            p.prepareToPlay (kSr, kBlock);
            if (! p.loadFile (f)) { std::printf ("  FAIL could not load %s source\n", label); ++fails; return; }
            const int blocks = 40;
            juce::AudioBuffer<float> out (1, blocks * kBlock);
            double peak = 0.0; bool finite = true;
            render (p, blocks, /*on=*/true, 60, /*off=*/false, out, peak, finite);
            std::printf ("%s source: peak=%.4f finite=%d\n", label, peak, finite ? 1 : 0);
            if (! finite)       { std::printf ("  FAIL %s source non-finite\n", label); ++fails; }
            if (peak > 1.0001)  { std::printf ("  FAIL %s source unbounded\n", label); ++fails; }
            if (peak <= 1.0e-3) { std::printf ("  FAIL %s source silent\n", label); ++fails; }
        };
        renderSource (monoSine,   "mono");
        renderSource (stereoSine, "stereo");
    }

    // Clean up temp files.
    stereoSine.deleteFile();
    monoSine.deleteFile();
    stereoNoise.deleteFile();

    std::printf (fails == 0 ? "Sattari Granulator contract ok\n"
                            : "Sattari Granulator contract FAILED (%d)\n", fails);
    return fails == 0 ? 0 : 1;
}
