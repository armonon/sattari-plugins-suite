#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using sattari::dsp::ScaleId;

juce::StringArray SattariShimmerAudioProcessor::intervalChoices()
{ return { "+12 (octave)", "+7 (fifth)", "+5 (fourth)" }; }

juce::StringArray SattariShimmerAudioProcessor::rootChoices()
{ return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
juce::StringArray SattariShimmerAudioProcessor::scaleChoices()
{ return { "major", "minor" }; }

int SattariShimmerAudioProcessor::baseSemisFor (int intervalIndex) noexcept
{
    switch (intervalIndex)
    {
        case 1:  return 7;    // perfect fifth
        case 2:  return 5;    // perfect fourth
        default: return 12;   // octave
    }
}

SattariShimmerAudioProcessor::SattariShimmerAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    registerFactoryPresets();
}
SattariShimmerAudioProcessor::~SattariShimmerAudioProcessor() { sessionReader.stop(); }

// Factory presets: real parameter values (0..1 amounts, choice index, ms). Only listed
// params are set; anything omitted keeps its default, so presets stay valid as params evolve.
void SattariShimmerAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Classic Octave",  PV{ {"size", 0.75f}, {"shimmer", 0.45f}, {"interval", 0}, {"damp", 0.35f}, {"predelay", 20}, {"width", 1.0f}, {"mix", 0.35f} });
    presetBank.addFactoryPreset ("Angelic Pad",     PV{ {"size", 0.90f}, {"shimmer", 0.60f}, {"interval", 0}, {"damp", 0.30f}, {"predelay", 40}, {"width", 1.0f}, {"mix", 0.50f} });
    presetBank.addFactoryPreset ("Fifth Bloom",     PV{ {"size", 0.70f}, {"shimmer", 0.50f}, {"interval", 1}, {"damp", 0.40f}, {"predelay", 25}, {"mix", 0.40f} });
    presetBank.addFactoryPreset ("Subtle Sparkle",  PV{ {"size", 0.60f}, {"shimmer", 0.30f}, {"interval", 0}, {"damp", 0.50f}, {"predelay", 15}, {"mix", 0.25f} });
    presetBank.addFactoryPreset ("Cathedral",       PV{ {"size", 0.95f}, {"shimmer", 0.70f}, {"interval", 0}, {"damp", 0.25f}, {"predelay", 60}, {"width", 1.0f}, {"mix", 0.55f} });
    presetBank.addFactoryPreset ("Fourth Glass",    PV{ {"size", 0.80f}, {"shimmer", 0.55f}, {"interval", 2}, {"damp", 0.35f}, {"predelay", 30}, {"mix", 0.45f} });
    presetBank.addFactoryPreset ("Dark Shimmer",    PV{ {"size", 0.85f}, {"shimmer", 0.50f}, {"interval", 0}, {"damp", 0.70f}, {"predelay", 35}, {"mix", 0.40f} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariShimmerAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<FP> ("size",    "Size / Decay", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f));
    p.push_back (std::make_unique<FP> ("shimmer", "Shimmer",      juce::NormalisableRange<float> (0.0f, 0.95f, 0.01f), 0.45f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("interval", "Interval", intervalChoices(), 0));
    p.push_back (std::make_unique<FP> ("damp",    "Damping / Tone", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));
    p.push_back (std::make_unique<FP> ("predelay","Pre-Delay",    juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 20.0f));
    p.push_back (std::make_unique<FP> ("width",   "Width",        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<FP> ("mix",     "Mix",          juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.35f));

    // Key Snap: quantize the shimmer interval to in-key scale tones from the bus.
    p.push_back (std::make_unique<juce::AudioParameterBool>   ("keysnap", "Key Snap", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("root",  "Key Root", rootChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Key Scale", scaleChoices(), 0));
    return { p.begin(), p.end() };
}

void SattariShimmerAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr;
    const int block = juce::jmax (1, samplesPerBlock);

    reverb.setSampleRate (sr);
    reverb.reset();

    dryScratch.setSize   (2, block);
    tankIn.setSize       (2, block);
    shiftScratch.setSize (2, block);
    fbCarry.setSize      (2, block);
    fbCarry.clear();

    // Pre-delay line: up to 200 ms + a block of slack.
    preLen = (int) std::ceil (sr * 0.2) + block + 8;
    for (auto& d : preBuf) d.assign ((size_t) preLen, 0.0f);
    preWrite = 0;

    // Formant-preserving shifter per channel, tuned for the musical vocal/pad
    // range the shimmer octave lands in.
    psola.assign (2, sattari::dsp::PsolaShifter());
    for (auto& s : psola) s.prepare (sr, 60.0, 1200.0);

    toneState = { 0.0f, 0.0f };
    sessionReader.start();
}
void SattariShimmerAudioProcessor::releaseResources() { sessionReader.stop(); }

bool SattariShimmerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariShimmerAudioProcessor::resolveKey (int& root, ScaleId& id) const
{
    root = (int) std::round (param ("root"));
    id   = ((int) std::round (param ("scale")) == 1) ? ScaleId::Minor : ScaleId::Major;

    // When Key Snap is on, follow the KEY bus if a fresh key is being published.
    if (param ("keysnap") > 0.5f)
        if (auto s = sessionReader.current(); s.valid)
        {
            root = juce::jlimit (0, 11, s.root);
            id   = (s.scale == 1) ? ScaleId::Minor : ScaleId::Major;   // bus: 0=maj,1=min
        }
}

// Quantize the base shimmer interval so the transposed pitch class is in-key.
// The bloom is transposition-invariant across octaves, so we snap the interval's
// pitch-class offset to the nearest scale tone (relative to the key root) and
// keep it inside a musical +3..+14 semitone window so the shimmer still rises.
int SattariShimmerAudioProcessor::effectiveShimmerSemis (int baseSemis, int keyRoot, ScaleId id) const
{
    if (id == ScaleId::ChromaticOff)
        return baseSemis;

    int n = 0;
    const int* iv = sattari::dsp::scaleIntervals (id, n);
    if (iv == nullptr || n <= 0)
        return baseSemis;

    // Snap a reference note (root + baseSemis) to the scale, take the resulting
    // interval. snapMidiToScale rounds to the nearest in-key pitch class.
    const int ref     = 60 + keyRoot + baseSemis;             // C5-ish reference + interval
    const int snapped = sattari::dsp::snapMidiToScale (ref, keyRoot, id);
    int semis = baseSemis + (snapped - ref);

    // Keep it an upward, musical shimmer (never fold to unison or below a minor 3rd).
    if (semis < 3)  semis += 12;
    if (semis > 14) semis -= 12;
    return juce::jlimit (3, 19, semis);
}

SattariShimmerAudioProcessor::KeyState SattariShimmerAudioProcessor::currentKeyState() const
{
    KeyState k;
    int root; ScaleId id; resolveKey (root, id);
    k.root    = root;
    k.scale   = (id == ScaleId::Minor) ? 1 : 0;
    k.snapping = param ("keysnap") > 0.5f;
    return k;
}

void SattariShimmerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0) return;

    const float  size    = param ("size");
    const float  shimmer = param ("shimmer");
    const float  damp    = param ("damp");

    // STABILITY BOUND on the pitch-shift feedback loop. The shimmer signal
    // recirculates THROUGH the reverb tank, whose summed-comb wet gain climbs
    // steeply with size (Freeverb comb feedback = roomSize*0.28+0.7). To keep the
    // round-trip gain strictly < 1 for EVERY parameter combination we (a) cap
    // roomSize hard (below), (b) throttle the injection as size rises, holding it
    // well under the reciprocal of the tank's worst-case wet gain, and (c) the
    // feedback path is always low-passed (a damping floor, below) so each pass
    // also sheds high end. Measured stable to silence at shimmer=0.95, size=1.0.
    const float shimmerInject = shimmer * (0.45f - 0.33f * size);   // 0.45 (small) .. 0.12 (large)
    const float  width   = param ("width");
    const float  mix     = param ("mix");
    const int    preSamp = juce::jlimit (0, preLen - 4,
                              (int) std::round (param ("predelay") * 0.001 * sampleRate));

    // --- Effective shimmer interval (optionally key-snapped) -----------------
    const int baseSemis = baseSemisFor ((int) std::round (param ("interval")));
    int keyRoot; ScaleId keyId; resolveKey (keyRoot, keyId);
    const int semis = (param ("keysnap") > 0.5f)
                        ? effectiveShimmerSemis (baseSemis, keyRoot, keyId)
                        : baseSemis;
    liveInterval.store (semis, std::memory_order_relaxed);
    const double pitchScale = std::pow (2.0, (double) semis / 12.0);   // upward transpose

    // --- Save the pure dry for our own mix, and build the tank input ---------
    // Tank input = pre-delayed dry + the shimmer feedback carried from the
    // previous block (already pitch-shifted + tone-damped). Pre-delay only the
    // dry path so the first reflections are pushed back but the shimmer stays
    // tight in the tail.
    for (int ch = 0; ch < numCh; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, n);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& pd = preBuf[(size_t) ch];
        const float* in  = buffer.getReadPointer (ch);
        const float* fb  = fbCarry.getReadPointer (ch);
        float*       tin = tankIn.getWritePointer (ch);
        int wp = preWrite;
        for (int i = 0; i < n; ++i)
        {
            pd[(size_t) wp] = in[i];
            int rp = wp - preSamp; if (rp < 0) rp += preLen;
            const float delayed = pd[(size_t) rp];
            tin[i] = delayed + shimmerInject * fb[i];   // inject last block's shimmer tail
            if (++wp >= preLen) wp = 0;
        }
    }
    preWrite += n; while (preWrite >= preLen) preWrite -= preLen;

    // --- Reverb tank (Freeverb), taken as PURE wet --------------------------
    // juce::Reverb scales dry x2 / wet x3 internally, so its dry/wet is not a
    // clean crossfade. Take dryLevel 0 and wetLevel 1/3 (=~unity wet), then do
    // our OWN dry/wet mix below so mix=0 is an exact pass-through (Space pattern).
    //
    // STABILITY: the tank sits inside the pitch-shift feedback loop, so the total
    // round-trip gain = (tank through-gain) * (shimmer injection). We cap roomSize
    // at 0.85 (Freeverb comb feedback = roomSize*0.28+0.7 -> <=0.938, always
    // dissipative; freeze is deliberately unreachable) so the tank alone cannot
    // sustain, and separately bound the shimmer injection below.
    const float sizeNorm = size;   // 0..1
    juce::Reverb::Parameters rp;
    rp.roomSize   = juce::jlimit (0.0f, 0.80f, 0.28f + 0.52f * sizeNorm);   // decay grows with size, capped
    rp.damping    = damp;
    rp.wetLevel   = 1.0f / 3.0f;
    rp.dryLevel   = 0.0f;
    rp.width      = width;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    for (int ch = 0; ch < numCh; ++ch)
        buffer.copyFrom (ch, 0, tankIn, ch, 0, n);

    if (numCh >= 2)
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    else
        reverb.processMono (buffer.getWritePointer (0), n);
    // `buffer` now holds the PURE wet tank output.

    // --- Build next block's shimmer feedback: shift the wet tail up, damp it -
    // Shift a copy of the wet tail (formant-preserving PSOLA), then run a one-
    // pole low-pass (the "tone/damp" control) over it so each recirculation loses
    // high end — this, together with the reverb decay and shimmer<1, keeps the
    // loop bounded (no runaway even at high shimmer + size).
    const double period = sampleRate / 220.0;   // mid reference period; PSOLA clamps internally
    // Feedback low-pass "tone": brighter as damp->0, darker as damp->1, but with a
    // real FLOOR (coeff <= 0.62) so the loop always sheds high end every pass —
    // part of the stability guarantee, not just a tone control.
    const float toneCoeff = juce::jlimit (0.02f, 0.62f, 0.62f - 0.55f * damp);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* wet = buffer.getReadPointer (ch);
        float* sh = shiftScratch.getWritePointer (ch);
        if (ch < (int) psola.size())
            psola[(size_t) ch].process (wet, sh, n, pitchScale, period);
        else
            std::copy (wet, wet + n, sh);

        float* fbOut = fbCarry.getWritePointer (ch);
        float st = toneState[(size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            st += toneCoeff * (sh[i] - st);          // one-pole LP (damping/tone + stability)
            if (! std::isfinite (st)) st = 0.0f;      // safety against denormal/NaN creep
            // Soft saturator in the loop: unity for small signals, compresses
            // large ones. Its small-signal gain is <= 1, so it cannot create a
            // self-sustaining limit cycle the way a hard clip can — the loop
            // still winds down to silence after the input stops.
            fbOut[i] = std::tanh (st);
        }
        toneState[(size_t) ch] = st;
    }
    if (numCh == 1) fbCarry.copyFrom (1, 0, fbCarry, 0, 0, n);   // keep ch1 sane in mono

    // --- Final dry/wet mix (mix=0 -> exact pass-through) ---------------------
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        const float* dry = dryScratch.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            float w = out[i];
            if (! std::isfinite (w)) w = 0.0f;
            out[i] = dry[i] * (1.0f - mix) + w * mix;
        }
    }
}

void SattariShimmerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariShimmerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariShimmerAudioProcessor::createEditor()
{
    return new SattariShimmerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariShimmerAudioProcessor();
}
