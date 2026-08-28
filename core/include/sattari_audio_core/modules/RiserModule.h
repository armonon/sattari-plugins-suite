#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  RiserModule — Sattari Riser's DSP, extracted so the standalone plugin and a
//  Create rack slot run the SAME build engine.
//
//  A tempo-locked transition builder. Arming (manually, or on the host's
//  play-start) starts a build that runs for a musical bar length, during which:
//    * a white-noise source sweeps through a resonant bandpass,
//    * a sine rises alongside it (Noise/Tone blends the two),
//    * the DRY input is pitch-glided by reading a short delay line back slowly,
//    * an accelerating stutter re-triggers ever-shorter grains,
//  all ramped by an accelerating curve so the build leans into the landing.
//  Downlifter runs the same shape backwards; Impact adds a decaying ~70 Hz thump
//  on landing.
//
//  When no build is in flight and no impact tail is decaying, the output is an
//  EXACT pass-through — Mix is ignored rather than applied to a silent wet,
//  which is what keeps the module inert in a rack slot between transitions.
//
//  On landing the module clears the `arm` latch through the parameter itself, so
//  a held toggle cannot retrigger immediately.
// =============================================================================

namespace sattari::dsp
{

class RiserModule final : public SattariModule,
                          private juce::AsyncUpdater
{
public:
    ~RiserModule() override
    {
        cancelPendingUpdate();
        tempoReader.stop();
    }

    static juce::StringArray modeChoices()   { return { "Riser", "Downlifter", "Impact" }; }
    static juce::StringArray lengthChoices() { return { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" }; }

    const char* moduleId()    const noexcept override { return "riser"; }
    const char* displayName() const noexcept override { return "Riser"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using R  = juce::NormalisableRange<float>;

        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "mode", 1), "Mode", modeChoices(), 0));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "length", 1), "Length", lengthChoices(), 1));
        // Arm is the manual trigger; a rising edge starts a build. hostTrigger arms
        // automatically on the host transport's play-start instead.
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "arm", 1), "Arm", false));
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "hostTrigger", 1), "Host Trigger", false));

        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "noiseTone", 1), "Noise / Tone", R (0.0f, 1.0f, 0.01f), 0.7f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "sweepLo", 1), "Sweep Low",  R (40.0f, 2000.0f, 1.0f, 0.35f), 180.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "sweepHi", 1), "Sweep High", R (500.0f, 18000.0f, 1.0f, 0.35f), 9000.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "reso", 1), "Resonance", R (0.3f, 6.0f, 0.01f), 2.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "pitchRise", 1), "Pitch Rise", R (0.0f, 24.0f, 0.1f), 12.0f,
                                             juce::AudioParameterFloatAttributes().withLabel (" st")));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "stutter", 1), "Stutter", R (0.0f, 1.0f, 0.01f), 0.4f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "mix", 1), "Mix", R (0.0f, 1.0f, 0.01f), 0.8f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "output", 1), "Output", R (-24.0f, 12.0f, 0.1f), 0.0f,
                                             juce::AudioParameterFloatAttributes().withLabel (" dB")));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sr;
        spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maxBlockSize);
        spec.numChannels = 1;               // mono noise source, sprayed to both channels
        noiseFilter.prepare (spec);
        noiseFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

        // Pitch-rise delay line: up to ~200 ms so a fast down-read still has history.
        pitchLen = (int) std::ceil (sr * 0.2) + 4;
        for (auto& b : pitchBuf) b.assign ((size_t) pitchLen, 0.0f);

        // Stutter capture buffer: up to ~0.5 s of recent audio per channel.
        stutLen = (int) std::ceil (sr * 0.5) + 4;
        for (auto& b : stutBuf) b.assign ((size_t) stutLen, 0.0f);

        tempoReader.start();
        reset();
    }

    void reset() override
    {
        noiseFilter.reset();
        for (auto& b : pitchBuf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : stutBuf)  std::fill (b.begin(), b.end(), 0.0f);
        pitchWriteI = 0;
        pitchRead[0] = pitchRead[1] = 0.0;
        stutWrite = 0;
        stutGrainSamples = juce::jmax (1, (int) (sr * 0.125));
        stutPos = 0;
        building = false;
        progress = 0.0;
        progressPerSample = 0.0;
        prevArm = false;
        prevPlaying = false;
        tonephase = 0.0;
        impactEnv = 0.0f;
        impactPhase = 0.0f;
        progressUi.store (0.0f, std::memory_order_relaxed);
        buildingUi.store (false, std::memory_order_relaxed);
        lastBpm.store (120.0, std::memory_order_relaxed);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override { transport = t; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const double bpm = resolveBpm();
        lastBpm.store (bpm, std::memory_order_relaxed);

        const int   mode      = (int) std::round (p ("mode"));
        const int   lengthSel = (int) std::round (p ("length"));
        const bool  arm       = p ("arm") > 0.5f;
        const bool  hostTrig  = p ("hostTrigger") > 0.5f;
        const float noiseTone = juce::jlimit (0.0f, 1.0f, p ("noiseTone"));
        const float sweepLo   = p ("sweepLo");
        const float sweepHi   = juce::jmax (sweepLo + 1.0f, p ("sweepHi"));
        const float reso      = juce::jlimit (0.3f, 6.0f, p ("reso"));
        const float pitchRise = juce::jlimit (0.0f, 24.0f, p ("pitchRise"));
        const float stutAmt   = juce::jlimit (0.0f, 1.0f, p ("stutter"));
        const float mix       = juce::jlimit (0.0f, 1.0f, p ("mix"));
        const float outGain   = juce::Decibels::decibelsToGain (p ("output"));

        // --- Trigger logic (edge-detected, so we never allocate here) ------------
        const bool armEdge  = arm && ! prevArm;
        const bool playEdge = hostTrig && transport.isPlaying && ! prevPlaying;
        prevArm = arm;
        prevPlaying = transport.isPlaying;
        if ((armEdge || playEdge) && ! building)
            startBuild (bpm, lengthSel);

        // Set the filter resonance once per block (cutoff is swept per sample).
        noiseFilter.setResonance (reso);

        const float nyq = (float) (sr * 0.5);
        const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float) sr;

        for (int i = 0; i < n; ++i)
        {
            // Gather dry mono for the pitch/stutter capture and per-channel dry.
            float dry[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < numCh; ++ch)
                dry[ch] = buffer.getReadPointer (ch)[i];
            if (numCh == 1) dry[1] = dry[0];

            // Always capture recent input into the pitch + stutter ring buffers so a
            // build that arms mid-note has history to read back.
            for (int ch = 0; ch < 2; ++ch)
            {
                pitchBuf[ch][(size_t) pitchWriteI] = dry[ch];
                stutBuf[ch][(size_t) stutWrite]    = dry[ch];
            }

            float wet[2] = { dry[0], dry[1] };

            if (building)
            {
                const float pr = (float) progress;                 // 0..1 linear build progress
                // "Shape" runs 0->1 (Riser/Impact) or 1->0 (Downlifter): the direction
                // everything sweeps. Riser/Impact rise into the beat; Downlifter falls.
                const bool  down  = (mode == 1);
                const float shape = down ? (1.0f - pr) : pr;
                // An accelerating curve so the build "leans" toward the landing.
                const float accel = shape * shape;

                // -- Rising resonant filter cutoff (exponential across the range) ---
                const float cutoff = juce::jlimit (20.0f, nyq * 0.98f,
                    sweepLo * std::pow (sweepHi / sweepLo, shape));
                noiseFilter.setCutoffFrequency (cutoff);

                // -- Noise source (white) through the sweeping bandpass -------------
                const float white = rng.nextFloat() * 2.0f - 1.0f;
                const float filtNoise = noiseFilter.processSample (0, white);

                // -- Tone: a sine that rises in pitch with the build ---------------
                const float toneHz = juce::jlimit (20.0f, nyq * 0.9f,
                    sweepLo * std::pow (sweepHi / sweepLo, shape * 0.75f));
                const float tone = std::sin ((float) tonephase);
                tonephase += (double) (twoPiOverSr * toneHz);
                if (tonephase > juce::MathConstants<double>::twoPi)
                    tonephase -= juce::MathConstants<double>::twoPi;

                // Blend noise vs tone, then ramp amplitude with the build.
                const float source = filtNoise * noiseTone + tone * (1.0f - noiseTone);
                // Amplitude envelope: quiet at the start, loud into the landing.
                const float amp = 0.02f + 0.85f * accel;
                const float synth = source * amp;

                // -- Pitch-rise on the DRY input -----------------------------------
                // Read the pitch buffer back at an increasing rate so the input glides
                // up (Riser) / down (Downlifter) by up to `pitchRise` semitones.
                float pitched[2] = { dry[0], dry[1] };
                if (pitchRise > 0.001f)
                {
                    const float semis = pitchRise * shape;
                    const double ratio = std::pow (2.0, semis / 12.0);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        double rp = pitchRead[ch];
                        int r0 = (int) std::floor (rp);
                        const float fr = (float) (rp - r0);
                        int i0 = ((pitchWriteI - 1 - r0) % pitchLen + pitchLen) % pitchLen;
                        int i1 = ((i0 - 1) % pitchLen + pitchLen) % pitchLen;
                        const float a = pitchBuf[ch][(size_t) i0];
                        const float b = pitchBuf[ch][(size_t) i1];
                        pitched[ch] = a * (1.0f - fr) + b * fr;
                        // Advance the read head; reading slower than we write raises pitch.
                        rp += ratio - 1.0;
                        // Keep the read lag bounded to the buffer.
                        if (rp > (double) pitchLen - 4.0) rp = (double) pitchLen - 4.0;
                        if (rp < 0.0) rp = 0.0;
                        pitchRead[ch] = rp;
                    }
                }

                // -- Accelerating stutter on the pitched input ---------------------
                float stut[2] = { pitched[0], pitched[1] };
                if (stutAmt > 0.001f)
                {
                    if (stutPos >= stutGrainSamples)
                    {
                        stutPos = 0;
                        // Grain shrinks with the build -> faster retrigger toward landing.
                        const float g = 0.15f - 0.14f * accel;     // 150 ms -> ~10 ms
                        stutGrainSamples = juce::jmax (1, (int) (g * sr));
                    }
                    // Read a frozen grain: the last `stutPos` samples before the grain
                    // start, held and repeated.
                    const int base = ((stutWrite - stutGrainSamples) % stutLen + stutLen) % stutLen;
                    const int rp = (base + stutPos) % stutLen;
                    for (int ch = 0; ch < 2; ++ch)
                        stut[ch] = stutBuf[ch][(size_t) rp];
                    ++stutPos;
                    // Crossfade dry->stutter by amount * accel so it ramps in.
                    const float sm = stutAmt * accel;
                    for (int ch = 0; ch < 2; ++ch)
                        stut[ch] = pitched[ch] * (1.0f - sm) + stut[ch] * sm;
                }

                // Wet = processed input (pitch+stutter) + synthesised sweep/tone.
                for (int ch = 0; ch < 2; ++ch)
                    wet[ch] = stut[ch] + synth;

                // Advance the build; on landing, trigger the resolution.
                progress += progressPerSample;
                if (progress >= 1.0)
                {
                    if (mode == 2)          // Impact: kick off a decaying broadband hit
                    {
                        impactEnv = 1.0f;
                        impactPhase = 0.0f;
                    }
                    finishBuild();
                    // Auto-clear the arm latch so a held toggle doesn't retrigger.
                    if (arm && ! armClearPending.exchange (true, std::memory_order_acq_rel))
                        triggerAsyncUpdate();
                }
            }

            // -- Impact tail: a short decaying noise-thump after an Impact landing --
            if (impactEnv > 1.0e-4f)
            {
                const float thumpNoise = (rng.nextFloat() * 2.0f - 1.0f);
                const float thumpTone  = std::sin (impactPhase);
                impactPhase += twoPiOverSr * 70.0f;               // ~70 Hz body
                const float hit = (thumpTone * 0.7f + thumpNoise * 0.3f) * impactEnv;
                for (int ch = 0; ch < 2; ++ch)
                    wet[ch] += hit;
                impactEnv *= 0.9992f;                              // ~decay over ~a second
                if (impactEnv < 1.0e-4f) impactEnv = 0.0f;
            }

            // Advance the shared write heads once per sample.
            if (++pitchWriteI >= pitchLen) pitchWriteI = 0;
            if (++stutWrite  >= stutLen)  stutWrite  = 0;

            // -- Mix + bounded output ------------------------------------------------
            const bool active = building || impactEnv > 1.0e-4f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                // When not building at all, this is an exact pass-through (mix ignored).
                float out = active ? (dry[ch] * (1.0f - mix) + wet[ch] * mix) : dry[ch];
                out *= outGain;
                // Transparent soft ceiling: tanh only bites past ~0.9, so quiet dry
                // passes through unchanged while a hot build can never run away.
                if (out > 0.9f || out < -0.9f)
                    out = std::tanh (out);
                buffer.getWritePointer (ch)[i] = out;
            }
        }

        progressUi.store ((float) progress, std::memory_order_relaxed);
        buildingUi.store (building, std::memory_order_relaxed);
    }

    // Lock-free UI readouts.
    double currentBpm()    const noexcept { return lastBpm.load (std::memory_order_relaxed); }
    float  buildProgress() const noexcept { return progressUi.load (std::memory_order_relaxed); }
    bool   isBuilding()    const noexcept { return buildingUi.load (std::memory_order_relaxed); }

private:
    void handleAsyncUpdate() override
    {
        if (! armClearPending.exchange (false, std::memory_order_acq_rel)
            || state == nullptr)
            return;

        if (auto* arm = state->getParameter (pfx + "arm"))
        {
            arm->beginChangeGesture();
            arm->setValueNotifyingHost (0.0f);
            arm->endChangeGesture();
        }
    }

    // Bar count for each length choice.
    static double barsFor (int len) noexcept
    {
        switch (len)
        {
            case 0: return 0.5;
            case 1: return 1.0;
            case 2: return 2.0;
            case 3: return 4.0;
            case 4: return 8.0;
            default: return 1.0;
        }
    }

    void startBuild (double bpm, int lengthBars)
    {
        const double bars = barsFor (lengthBars);
        const double beats = bars * 4.0;                       // assume 4 beats / bar
        const double buildSamples = juce::jmax (1.0, beats * (60.0 / bpm) * sr);
        progressPerSample = 1.0 / buildSamples;
        progress = 0.0;
        building = true;
        tonephase = 0.0;
        noiseFilter.reset();
        // Reset the pitch read lag so each build glides from unison (0 lag) upward.
        pitchRead[0] = pitchRead[1] = 0.0;
        // Re-seed the stutter grain size for the start of the build (coarse).
        stutGrainSamples = juce::jmax (1, (int) (sr * 0.125));
        stutPos = 0;
    }

    void finishBuild()
    {
        building = false;
        progress = 0.0;
    }

    double resolveBpm() noexcept
    {
        if (transport.hasHostBpm && transport.bpm > 1.0) return transport.bpm;
        if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0) return s.bpm;   // TEMPO bus fallback
        return 120.0;
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    sattari::xplugin::TempoReader tempoReader;   // TEMPO-bus fallback when host reports none
    sattari::host::TransportSnapshot transport;
    std::atomic<double> lastBpm { 120.0 };

    // --- Build engine state (audio thread only) ------------------------------
    bool   building = false;    // a build is currently in flight
    double progress = 0.0;      // 0..1 over the chosen bar length
    double progressPerSample = 0.0;
    bool   prevArm = false;     // edge-detect the arm toggle
    bool   prevPlaying = false; // edge-detect host transport start (host-trigger mode)

    // Filtered-noise sweep.
    juce::dsp::StateVariableTPTFilter<float> noiseFilter;
    juce::Random rng;

    // Tone oscillator (rising sine for the "tonal" half of the build).
    double tonephase = 0.0;

    // Pitch-rise on the input: a short delay line read back at a varying rate.
    std::vector<float> pitchBuf[2];
    int    pitchLen = 0;
    int    pitchWriteI = 0;
    double pitchRead[2] { 0.0, 0.0 };

    // Accelerating stutter (a re-triggered capture-and-hold gate).
    std::vector<float> stutBuf[2];
    int    stutLen = 0;
    int    stutWrite = 0;
    int    stutGrainSamples = 0;     // current grain length (shrinks as build accelerates)
    int    stutPos = 0;              // playback position within the current grain

    // Landing "impact" tail.
    float  impactEnv = 0.0f;
    float  impactPhase = 0.0f;

    std::atomic<float> progressUi { 0.0f };
    std::atomic<bool>  buildingUi { false };
    std::atomic<bool>  armClearPending { false };
};

} // namespace sattari::dsp
