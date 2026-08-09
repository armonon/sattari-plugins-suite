#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

// The 808 body lives in the sub range; clamp so a mistracked octave never sends
// the oscillator above the low-bass region or into the infrasonic mud.
float clampBodyHz (float hz) noexcept
{
    return juce::jlimit (20.0f, 160.0f, hz);
}

float softClip (float x) noexcept
{
    return std::tanh (x);
}

// Bus scale convention (matches Auto Pitch / Sattari Key): 0 major, 1 minor,
// 2 chromatic. Map onto the shared scale-snap ids.
sattari::dsp::ScaleId busScaleToId (int busScale) noexcept
{
    switch (busScale)
    {
        case 0:  return sattari::dsp::ScaleId::Major;
        case 1:  return sattari::dsp::ScaleId::Minor;
        default: return sattari::dsp::ScaleId::ChromaticOff;   // 2 = chromatic / unknown
    }
}
} // namespace

Sattari808AudioProcessor::Sattari808AudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout Sattari808AudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Body waveform: a pure sine (deep, clean) or a triangle (a touch more edge
    // that survives small speakers).
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("wave", "Body Wave",
        juce::StringArray { "Sine", "Triangle" }, 0));

    // Tune-to-key snap: quantise the tracked fundamental to the KEY-bus scale so
    // the re-synthesised 808 always sits in the current song key. Default OFF so
    // out of the box the body follows the raw tracked pitch; the editor greys the
    // menu the same way the rest of the suite does. When on with no fresh key on
    // the bus, the body still follows raw pitch (nothing to snap to).
    params.push_back (std::make_unique<juce::AudioParameterBool> ("snap", "Tune to Key", false));

    // Glide / portamento toward each new tracked pitch (0 ~ instant, 1 ~ 300 ms).
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("glide", "Glide",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.15f));

    // Amp decay: how long the 808 body tail rings after each retrigger.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("decay", "Amp Decay",
        juce::NormalisableRange<float> (0.05f, 2.50f, 0.001f, 0.5f), 0.60f));

    // Drive / click: the attack transient "click" plus body saturation.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("drive", "Drive / Click",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.30f));

    // Sub-fold / harmonics: mixes in a one-octave-down fold plus a little 2nd/3rd
    // harmonic weight for the classic distorted-sub growl.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("subfold", "Sub Fold",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f));

    // Blend: dry/wet between the untouched input (0) and the 808 body (1).
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("blend", "Blend",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.75f));

    // Output trim.
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("output", "Output",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));

    return { params.begin(), params.end() };
}

void Sattari808AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    oscillatorPhase = 0.0f;
    smoothedHz = 55.0f;
    targetHz = 55.0f;
    ampEnv = 0.0f;
    clickEnv = 0.0f;
    inputEnvFast = 0.0f;
    noteActive = false;
    limiterState = 1.0f;

    monoScratch.assign ((size_t) juce::jmax (samplesPerBlock, 32), 0.0f);
    sessionReader.start();   // poll the cross-plugin session bus off the audio thread

    // Long low-frequency analysis window (>= one period at the 25 Hz floor).
    pitchDetector.prepare (sampleRate, 25.0f, 300.0f, 0.090f);
    analysisAccumulator = 0;
    analysisHop = juce::jmax (512, samplesPerBlock);

    // Sample-rate-correct limiter recovery (~20 ms).
    limiterRecoveryCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.02));

    displayHz.store (smoothedHz, std::memory_order_relaxed);
}

bool Sattari808AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && in == out;
}

void Sattari808AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if ((int) monoScratch.size() < numSamples)
        return; // host exceeded the prepared block size; keep the dry signal

    // Mono sum of the input for pitch tracking + gating.
    std::fill (monoScratch.begin(), monoScratch.begin() + numSamples, 0.0f);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* input = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            monoScratch[(size_t) i] += input[i] / (float) numChannels;
    }

    const int   wave    = (int) *apvts.getRawParameterValue ("wave");
    const bool  snap    = apvts.getRawParameterValue ("snap")->load() > 0.5f;
    const float glide   = *apvts.getRawParameterValue ("glide");
    const float decay   = *apvts.getRawParameterValue ("decay");
    const float drive   = *apvts.getRawParameterValue ("drive");
    const float subfold = *apvts.getRawParameterValue ("subfold");
    const float blend   = *apvts.getRawParameterValue ("blend");
    const float output  = *apvts.getRawParameterValue ("output");

    // Feed this block's mono sum into the shared pitch detector.
    pitchDetector.pushBlock (monoScratch.data(), numSamples);

    // Re-estimate the fundamental on a decimated schedule (analysis is heavy).
    analysisAccumulator += numSamples;
    float wantedHz = targetHz;
    bool  haveFresh = false;
    if (analysisAccumulator >= analysisHop)
    {
        analysisAccumulator = 0;
        const auto detected = pitchDetector.analyse (0.22f);
        if (detected.voiced && detected.frequencyHz > 0.0f)
        {
            // Octave-continuity guard: fold a fresh estimate that leaps more than
            // ~a fifth back toward the running pitch so the body never octave-jumps
            // on a mis-detection.
            wantedHz = sattari::dsp::foldToOctaveNear (detected.frequencyHz, smoothedHz);
            haveFresh = true;
        }
    }

    // A live MIDI note-on overrides the tracked pitch for that block (play the 808
    // from a keyboard on top of the follow).
    for (const auto metadata : midi)
        if (metadata.getMessage().isNoteOn())
        {
            wantedHz = sattari::dsp::midiToHz ((float) metadata.getMessage().getNoteNumber());
            haveFresh = true;
        }

    // Tune-to-key: snap the tracked note to the nearest scale tone of the current
    // KEY-bus session (root + scale), then convert back to Hz. No fresh bus key ->
    // pass the tracked pitch through unchanged.
    if (snap && wantedHz > 0.0f)
    {
        if (const auto s = sessionReader.current(); s.valid)
        {
            const int rawMidi = sattari::dsp::nearestMidiNote (wantedHz);
            if (rawMidi >= 0)
            {
                const int snapped = sattari::dsp::snapMidiToScale (rawMidi, s.root, busScaleToId (s.scale));
                wantedHz = sattari::dsp::midiToHz ((float) snapped);
            }
        }
    }

    targetHz = clampBodyHz (wantedHz);

    // Time-based portamento coefficient (glide=0 ~ instant, 1 ~ 300 ms).
    const float glideTime = 0.0005f + glide * 0.30f;
    const float glideCoeff = 1.0f - std::exp (-1.0f / (float) (currentSampleRate * glideTime));

    // Amp-decay coefficient: exponential per-sample decay toward zero. The decay
    // parameter is the ~-60 dB time in seconds.
    const float decayCoeff = std::exp (-1.0f / (float) (currentSampleRate * juce::jmax (0.01, (double) decay / 6.9)));
    // Click transient decays fast (~6 ms) regardless of body decay.
    const float clickCoeff = std::exp (-1.0f / (float) (currentSampleRate * 0.006));
    // Fast input follower coefficient for gating/retrigger (~5 ms).
    const float envCoeff = 1.0f - std::exp (-1.0f / (float) (currentSampleRate * 0.005));

    // Retrigger thresholds (hysteresis so a sustained note doesn't chatter).
    constexpr float onThresh  = 0.02f;
    constexpr float offThresh = 0.008f;

    const float driveK = 1.0f + drive * 6.0f;
    const float driveNorm = std::tanh (driveK);
    const float harm2 = subfold * 0.30f;   // 2nd harmonic weight
    const float subMix = subfold * 0.6f;   // one-octave-down fold weight

    for (int i = 0; i < numSamples; ++i)
    {
        const float in = monoScratch[(size_t) i];
        const float rect = std::abs (in);

        // Fast input follower for gating.
        inputEnvFast += envCoeff * (rect - inputEnvFast);

        // Retrigger the 808 body on a fresh onset (input rising through the on
        // threshold from below the off threshold). Reset the amp env to full and
        // fire the attack click; re-align phase so each hit starts clean.
        if (! noteActive && inputEnvFast > onThresh)
        {
            noteActive = true;
            ampEnv = 1.0f;
            clickEnv = 1.0f;
            oscillatorPhase = 0.0f;
        }
        else if (noteActive && inputEnvFast < offThresh)
        {
            noteActive = false;   // let the tail ring out; next onset retriggers
        }

        // Decay the envelopes.
        ampEnv *= decayCoeff;
        clickEnv *= clickCoeff;

        // Per-sample portamento toward the target pitch.
        smoothedHz += glideCoeff * (targetHz - smoothedHz);

        const float phaseInc = twoPi * smoothedHz / (float) currentSampleRate;
        oscillatorPhase += phaseInc;
        if (oscillatorPhase >= twoPi)
            oscillatorPhase -= twoPi;

        // 808 body: sine or triangle fundamental.
        float body;
        if (wave == 1)
        {
            // Triangle from phase [0, 2pi): rises to +1 at pi, back to -1.
            const float t = oscillatorPhase / twoPi;         // [0, 1)
            body = 4.0f * std::abs (t - 0.5f) - 1.0f;         // [-1, 1], triangle
        }
        else
        {
            body = std::sin (oscillatorPhase);
        }

        // Sub-harmonic fold one octave down (half-frequency sine) + a little 2nd
        // harmonic for growl.
        body += subMix * std::sin (0.5f * oscillatorPhase);
        body += harm2 * std::sin (2.0f * oscillatorPhase);

        // Body saturation / drive (normalised so drive adds harmonics without a
        // level jump).
        body = std::tanh (body * driveK) / driveNorm;

        // Attack click: a short high-passed transient burst on retrigger, scaled
        // by drive so "Drive / Click" governs both.
        const float click = clickEnv * clickEnv * (drive * 0.9f) * std::sin (oscillatorPhase * 6.0f);

        float wet = (body * ampEnv + click) * 0.9f;

        // Per-sample limiter so the wet body never runs past the ceiling.
        const float peak = std::abs (wet);
        constexpr float ceiling = 0.95f;
        if (peak > ceiling)
            limiterState = juce::jmin (limiterState, ceiling / (peak + 1.0e-9f));
        else
            limiterState += limiterRecoveryCoeff * (1.0f - limiterState);
        wet *= limiterState;

        // Apply the output trim + soft-clip ceiling to the WET body only, so the
        // dry signal stays untouched. Blending in the *processed* wet keeps the
        // wet path's behaviour identical to before (at blend=1 this is exactly
        // softClip(wet * output)), while blend=0 collapses to an exact dry
        // pass-through: mixed = dry, sample-for-sample.
        const float wetOut = juce::jlimit (-1.5f, 1.5f, softClip (wet * output));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = buffer.getWritePointer (ch);
            const float dry = out[i];
            out[i] = (1.0f - blend) * dry + blend * wetOut;
        }
    }

    displayHz.store (smoothedHz, std::memory_order_relaxed);
    juce::ignoreUnused (haveFresh);
}

juce::AudioProcessorEditor* Sattari808AudioProcessor::createEditor()
{
    return new Sattari808AudioProcessorEditor (*this);
}

void Sattari808AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void Sattari808AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Sattari808AudioProcessor();
}
