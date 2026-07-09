#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::StringArray SattariRiserAudioProcessor::modeChoices()
{ return { "Riser", "Downlifter", "Impact" }; }

juce::StringArray SattariRiserAudioProcessor::lengthChoices()
{ return { "1/2 bar", "1 bar", "2 bars", "4 bars", "8 bars" }; }

// Bar count for each length choice.
static double barsFor (int len)
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

SattariRiserAudioProcessor::SattariRiserAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}
SattariRiserAudioProcessor::~SattariRiserAudioProcessor() { tempoReader.stop(); }

juce::AudioProcessorValueTreeState::ParameterLayout
SattariRiserAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterChoice> ("mode", "Mode", modeChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("length", "Length", lengthChoices(), 1));
    // Arm is the manual trigger; a rising edge starts a build. hostTrigger arms
    // automatically on the host transport's play-start instead.
    p.push_back (std::make_unique<juce::AudioParameterBool> ("arm", "Arm", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> ("hostTrigger", "Host Trigger", false));

    p.push_back (std::make_unique<FP> ("noiseTone", "Noise / Tone", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.7f));
    p.push_back (std::make_unique<FP> ("sweepLo", "Sweep Low",  juce::NormalisableRange<float> (40.0f, 2000.0f, 1.0f, 0.35f), 180.0f));
    p.push_back (std::make_unique<FP> ("sweepHi", "Sweep High", juce::NormalisableRange<float> (500.0f, 18000.0f, 1.0f, 0.35f), 9000.0f));
    p.push_back (std::make_unique<FP> ("reso", "Resonance", juce::NormalisableRange<float> (0.3f, 6.0f, 0.01f), 2.0f));
    p.push_back (std::make_unique<FP> ("pitchRise", "Pitch Rise", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 12.0f, juce::AudioParameterFloatAttributes().withLabel (" st")));
    p.push_back (std::make_unique<FP> ("stutter", "Stutter", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.4f));
    p.push_back (std::make_unique<FP> ("mix", "Mix", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.8f));
    p.push_back (std::make_unique<FP> ("output", "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel (" dB")));

    return { p.begin(), p.end() };
}

void SattariRiserAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, samplesPerBlock);
    spec.numChannels = 1;               // mono noise source, sprayed to both channels
    noiseFilter.prepare (spec);
    noiseFilter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
    noiseFilter.reset();

    // Pitch-rise delay line: up to ~120 ms so a fast down-read still has history.
    pitchLen = (int) std::ceil (sampleRate * 0.2) + 4;
    for (auto& b : pitchBuf) b.assign ((size_t) pitchLen, 0.0f);
    pitchWriteI = 0;
    pitchRead[0] = pitchRead[1] = 0.0;

    // Stutter capture buffer: up to ~0.5 s of recent audio per channel.
    stutLen = (int) std::ceil (sampleRate * 0.5) + 4;
    for (auto& b : stutBuf) b.assign ((size_t) stutLen, 0.0f);
    stutWrite = 0;
    stutGrainSamples = juce::jmax (1, (int) (sampleRate * 0.125));
    stutPos = 0;

    tempoReader.start();
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
}

void SattariRiserAudioProcessor::releaseResources() { tempoReader.stop(); }

bool SattariRiserAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double SattariRiserAudioProcessor::resolveBpm()
{
    const auto t = sattari::host::read (*this);
    if (t.hasHostBpm && t.bpm > 1.0)
        return t.bpm;
    if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)   // TEMPO bus fallback
        return s.bpm;
    return 120.0;
}

void SattariRiserAudioProcessor::startBuild (double bpm, int lengthBars)
{
    const double bars = barsFor (lengthBars);
    const double beats = bars * 4.0;                       // assume 4 beats / bar
    const double buildSamples = juce::jmax (1.0, beats * (60.0 / bpm) * sampleRate);
    progressPerSample = 1.0 / buildSamples;
    progress = 0.0;
    building = true;
    tonephase = 0.0;
    noiseFilter.reset();
    // Reset the pitch read lag so each build glides from unison (0 lag) upward.
    pitchRead[0] = pitchRead[1] = 0.0;
    // Re-seed the stutter grain size for the start of the build (coarse).
    stutGrainSamples = juce::jmax (1, (int) (sampleRate * 0.125));
    stutPos = 0;
}

void SattariRiserAudioProcessor::finishBuild()
{
    building = false;
    progress = 0.0;
}

void SattariRiserAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (numCh <= 0) return;

    const double bpm = resolveBpm();
    lastBpm.store (bpm, std::memory_order_relaxed);

    const int   mode      = (int) std::round (param ("mode"));
    const int   lengthSel = (int) std::round (param ("length"));
    const bool  arm       = param ("arm") > 0.5f;
    const bool  hostTrig  = param ("hostTrigger") > 0.5f;
    const float noiseTone = juce::jlimit (0.0f, 1.0f, param ("noiseTone"));
    const float sweepLo   = param ("sweepLo");
    const float sweepHi   = juce::jmax (sweepLo + 1.0f, param ("sweepHi"));
    const float reso      = juce::jlimit (0.3f, 6.0f, param ("reso"));
    const float pitchRise = juce::jlimit (0.0f, 24.0f, param ("pitchRise"));
    const float stutAmt   = juce::jlimit (0.0f, 1.0f, param ("stutter"));
    const float mix       = juce::jlimit (0.0f, 1.0f, param ("mix"));
    const float outGain   = juce::Decibels::decibelsToGain (param ("output"));

    // --- Trigger logic (edge-detected, so we never allocate here) ------------
    const auto t = sattari::host::read (*this);
    const bool armEdge = arm && ! prevArm;
    const bool playEdge = hostTrig && t.isPlaying && ! prevPlaying;
    prevArm = arm;
    prevPlaying = t.isPlaying;
    if ((armEdge || playEdge) && ! building)
        startBuild (bpm, lengthSel);

    // Set the filter resonance once per block (cutoff is swept per sample).
    noiseFilter.setResonance (reso);

    const float nyq = (float) (sampleRate * 0.5);
    const float twoPiOverSr = juce::MathConstants<float>::twoPi / (float) sampleRate;

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
                    stutGrainSamples = juce::jmax (1, (int) (g * sampleRate));
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
                if (auto* ap = apvts.getParameter ("arm"))
                    if (arm) ap->setValueNotifyingHost (0.0f);
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

void SattariRiserAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariRiserAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariRiserAudioProcessor::createEditor()
{
    return new SattariRiserAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariRiserAudioProcessor();
}
