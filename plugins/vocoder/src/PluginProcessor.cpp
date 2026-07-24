#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using sattari::dsp::ScaleId;

namespace { inline double clampF (double f, double sr) { return juce::jlimit (20.0, sr * 0.45, f); } }

// =============================================================================
//  Biquad — bandpass (constant skirt gain) + high-pass, RBJ cookbook (mirrors EQ)
// =============================================================================
void SattariVocoderAudioProcessor::Biquad::setBandpass (double f, double Q, double sr) noexcept
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * juce::jmax (0.1, Q));
    const double a0 = 1.0 + alpha;
    b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0; a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
}
void SattariVocoderAudioProcessor::Biquad::setHP (double f, double sr) noexcept
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * 0.707);
    const double a0 = 1.0 + alpha;
    b0 = ((1.0 + c) * 0.5) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) * 0.5) / a0;
    a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
}

// =============================================================================
//  BlepOsc — band-limited saw / pulse via polyBLEP
// =============================================================================
float SattariVocoderAudioProcessor::BlepOsc::next (int wave) noexcept
{
    phase += inc;
    if (phase >= 1.0) phase -= 1.0;

    const double dt = inc;
    auto poly = [dt] (double t) noexcept -> double
    {
        if (dt <= 0.0) return 0.0;
        if (t < dt)          { t /= dt;        return t + t - t * t - 1.0; }
        else if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    };

    if (wave == 0)   // saw
    {
        double v = 2.0 * phase - 1.0;
        v -= poly (phase);
        return (float) v;
    }
    // pulse / square (50% duty)
    double v = phase < 0.5 ? 1.0 : -1.0;
    v += poly (phase);
    double t2 = phase + 0.5; if (t2 >= 1.0) t2 -= 1.0;
    v -= poly (t2);
    return (float) v;
}

// =============================================================================
//  Choices
// =============================================================================
juce::StringArray SattariVocoderAudioProcessor::rootChoices()
{ return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
juce::StringArray SattariVocoderAudioProcessor::scaleChoices()
{ return { "Chromatic", "Major", "Minor", "Penta Maj", "Penta Min", "Dorian", "Mixolydian", "Harm Minor" }; }
juce::StringArray SattariVocoderAudioProcessor::waveChoices()
{ return { "Saw", "Pulse" }; }
juce::StringArray SattariVocoderAudioProcessor::voicingChoices()
{ return { "Root", "Octaves", "Fifths", "Triad", "Seventh" }; }

SattariVocoderAudioProcessor::SattariVocoderAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    heldMidi.fill (-1);
    registerFactoryPresets();
}

SattariVocoderAudioProcessor::~SattariVocoderAudioProcessor() { sessionReader.stop(); }

// Factory presets: real parameter values (band count, choice index, semitones, ms, dB).
// Only listed params are set; anything omitted keeps its default. Key params (followKey /
// root / scale) are intentionally left untouched so a preset never fights the song key.
void SattariVocoderAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("Classic Robot",   PV{ {"bands", 20}, {"wave", 0}, {"voicing", 1}, {"octave",  0}, {"formant",  0}, {"unvoiced", 0.30f}, {"attack",  5}, {"release",  60}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Fat Synth Choir", PV{ {"bands", 28}, {"wave", 0}, {"voicing", 3}, {"octave",  0}, {"formant",  2}, {"unvoiced", 0.25f}, {"attack",  8}, {"release", 120}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Bright Talk",     PV{ {"bands", 32}, {"wave", 1}, {"voicing", 0}, {"octave",  0}, {"formant",  3}, {"unvoiced", 0.50f}, {"attack",  3}, {"release",  40}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Deep Menace",     PV{ {"bands", 16}, {"wave", 0}, {"voicing", 2}, {"octave", -1}, {"formant", -4}, {"unvoiced", 0.20f}, {"attack", 10}, {"release", 150}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Airy Sibilant",   PV{ {"bands", 24}, {"wave", 0}, {"voicing", 1}, {"octave",  1}, {"formant",  4}, {"unvoiced", 0.70f}, {"attack",  2}, {"release",  30}, {"mix", 1.0f}, {"output", 0} });
    presetBank.addFactoryPreset ("Seventh Pad",     PV{ {"bands", 26}, {"wave", 0}, {"voicing", 4}, {"octave",  0}, {"formant",  0}, {"unvoiced", 0.30f}, {"attack", 15}, {"release", 200}, {"mix", 1.0f}, {"output", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariVocoderAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterInt>    ("bands", "Bands", 8, 32, 16));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("wave", "Carrier Wave", waveChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("voicing", "Voicing", voicingChoices(), 1));
    p.push_back (std::make_unique<juce::AudioParameterInt>    ("octave", "Carrier Octave", -2, 2, 0));
    p.push_back (std::make_unique<FP> ("formant", "Formant Shift",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));                 // semitones of band shift
    p.push_back (std::make_unique<FP> ("unvoiced", "Unvoiced / Sibilance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.30f));
    p.push_back (std::make_unique<FP> ("attack", "Follower Attack",
        juce::NormalisableRange<float> (0.5f, 50.0f, 0.1f, 0.4f), 5.0f));             // ms
    p.push_back (std::make_unique<FP> ("release", "Follower Release",
        juce::NormalisableRange<float> (5.0f, 400.0f, 0.1f, 0.4f), 60.0f));           // ms
    p.push_back (std::make_unique<FP> ("mix", "Dry / Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));                   // 0 = dry (vocal), 1 = wet
    p.push_back (std::make_unique<FP> ("output", "Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));                 // dB

    // Key control (matches the shared KeyButton convention across the suite).
    p.push_back (std::make_unique<juce::AudioParameterBool>   ("followKey", "Follow Song Key", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("root",  "Root",  rootChoices(), 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale", scaleChoices(), 2)); // Minor

    return { p.begin(), p.end() };
}

// Log-spaced band centres over the classic vocoder range (~110 Hz .. 8 kHz).
void SattariVocoderAudioProcessor::computeBandFrequencies (int bands) noexcept
{
    bands = juce::jlimit (8, kMaxBands, bands);
    const double lo = 110.0, hi = juce::jmin (8000.0, sampleRate * 0.45);
    const double ratio = std::pow (hi / lo, 1.0 / juce::jmax (1, bands - 1));
    double f = lo;
    for (int i = 0; i < bands; ++i) { bandFreq[(size_t) i] = (float) f; f *= ratio; }
    activeBands = bands;
}

void SattariVocoderAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    computeBandFrequencies ((int) std::round (param ("bands")));
    for (auto& b : modBP) b.reset();
    for (auto& b : carBP) b.reset();
    for (auto& e : bandEnv) { e.prepare (sampleRate, 5.0f, 60.0f); e.reset (0.0f); }
    for (auto& b : unvoicedHP) b.reset();
    for (auto& v : voices) { v.osc.reset(); v.active = false; v.note = -1; v.freq = 0.0f; }

    heldMidi.fill (-1);
    heldMidiCount = 0;

    smWet      = param ("mix");
    smUnvoiced = param ("unvoiced");

    sessionReader.start();   // poll the cross-plugin key bus off the audio thread
}

void SattariVocoderAudioProcessor::releaseResources() { sessionReader.stop(); }

bool SattariVocoderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariVocoderAudioProcessor::resolveKey (int& root, ScaleId& id) const
{
    root = (int) std::round (param ("root"));
    id   = (ScaleId) (int) std::round (param ("scale"));
    if (param ("followKey") > 0.5f)
        if (auto s = sessionReader.current(); s.valid)
        {
            root = juce::jlimit (0, 11, s.root);
            id   = (s.scale == 1) ? ScaleId::Minor : ScaleId::Major;   // bus: 0=maj,1=min
        }
}

SattariVocoderAudioProcessor::KeyState SattariVocoderAudioProcessor::currentKeyState() const
{
    KeyState k;
    int root; ScaleId id; resolveKey (root, id);
    k.root = root; k.scale = (int) id;
    k.following = param ("followKey") > 0.5f;
    return k;
}

// Build carrier MIDI notes. If the host is sending held MIDI notes, those win
// (the player is fingering the chord). Otherwise auto-voice an in-key chord.
int SattariVocoderAudioProcessor::carrierNotes (int* out, int maxOut, int root, ScaleId id,
                                                int voicing, int octave) const
{
    int c = 0;
    auto add = [&] (int n) { if (c < maxOut && n >= 0 && n <= 127) out[c++] = n; };

    const int oct = octave * 12;

    if (heldMidiCount > 0)
    {
        // Newest-first so a fast chord still fits within kMaxCarrier voices.
        for (int i = heldMidiCount - 1; i >= 0 && c < maxOut; --i)
            if (heldMidi[(size_t) i] >= 0) add (juce::jlimit (0, 127, heldMidi[(size_t) i] + oct));
        return c;
    }

    // Auto-voice: root in a comfortable baritone register, build by scale degree.
    const int base = 48 + root + oct;   // C3-ish
    int n = 0;
    const int* iv = sattari::dsp::scaleIntervals (id, n);
    auto degree = [&] (int step) -> int
    {
        if (iv == nullptr || n <= 0) return base + step;   // chromatic fallback
        int d = step, o = 0;
        while (d >= n) { d -= n; ++o; }
        while (d < 0)  { d += n; --o; }
        return base + o * 12 + iv[d];
    };

    switch (voicing)
    {
        case 0: add (base); break;                                  // Root
        case 1: add (base); add (base + 12); break;                 // Octaves
        case 2: add (base); add (base + 7); add (base + 12); break; // Fifths
        case 3: add (degree (0)); add (degree (2)); add (degree (4)); break;             // Triad
        case 4: add (degree (0)); add (degree (2)); add (degree (4)); add (degree (6)); break; // Seventh
        default: add (base); break;
    }
    return c;
}

void SattariVocoderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    // --- Absorb incoming MIDI (host player fingering the carrier chord) -------
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getVelocity() > 0)
        {
            const int note = m.getNoteNumber();
            bool present = false;
            for (int i = 0; i < heldMidiCount; ++i) if (heldMidi[(size_t) i] == note) { present = true; break; }
            if (! present && heldMidiCount < (int) heldMidi.size())
                heldMidi[(size_t) heldMidiCount++] = note;
        }
        else if (m.isNoteOff() || (m.isNoteOn() && m.getVelocity() == 0))
        {
            const int note = m.getNoteNumber();
            for (int i = 0; i < heldMidiCount; ++i)
                if (heldMidi[(size_t) i] == note)
                {
                    for (int j = i; j < heldMidiCount - 1; ++j) heldMidi[(size_t) j] = heldMidi[(size_t) (j + 1)];
                    heldMidi[(size_t) --heldMidiCount] = -1;
                    break;
                }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldMidi.fill (-1); heldMidiCount = 0;
        }
    }

    // --- Per-block parameter read + coefficient update ------------------------
    const int reqBands = juce::jlimit (8, kMaxBands, (int) std::round (param ("bands")));
    if (reqBands != activeBands)
    {
        computeBandFrequencies (reqBands);
        for (int i = 0; i < activeBands; ++i) { modBP[(size_t) i].reset(); carBP[(size_t) i].reset(); }
    }

    const int   wave      = juce::jlimit (0, 1, (int) std::round (param ("wave")));
    const int   voicing   = juce::jlimit (0, 4, (int) std::round (param ("voicing")));
    const int   octave    = juce::jlimit (-2, 2, (int) std::round (param ("octave")));
    const float attackMs  = param ("attack");
    const float releaseMs = param ("release");
    const float outGain   = juce::Decibels::decibelsToGain (param ("output"));
    const float tgtWet      = juce::jlimit (0.0f, 1.0f, param ("mix"));
    const float tgtUnvoiced = juce::jlimit (0.0f, 1.0f, param ("unvoiced"));
    const float formantSemis = param ("formant");

    for (auto& e : bandEnv) e.setTimes (attackMs, releaseMs);

    int root; ScaleId id; resolveKey (root, id);

    // Choose carrier notes and (re)assign to fixed voices. formant applies a
    // per-band centre-frequency shift on the CARRIER bank (shifts the spectral
    // envelope up/down without changing the pitch the carrier plays).
    const double formantMul = std::pow (2.0, (double) formantSemis / 12.0);

    int noteBuf[kMaxCarrier];
    const int nNotes = carrierNotes (noteBuf, kMaxCarrier, root, id, voicing, octave);
    for (int v = 0; v < kMaxCarrier; ++v)
    {
        if (v < nNotes)
        {
            const int note = noteBuf[v];
            if (! voices[(size_t) v].active || voices[(size_t) v].note != note)
            {
                voices[(size_t) v].note = note;
                voices[(size_t) v].freq = sattari::dsp::midiToHz ((float) note);
                voices[(size_t) v].active = true;
                // keep phase running to avoid clicks
            }
            voices[(size_t) v].osc.setFreq (voices[(size_t) v].freq, sampleRate);
        }
        else
        {
            voices[(size_t) v].active = false;
        }
    }

    // Bandpass coefficients: modulator uses band centre, carrier uses the
    // formant-shifted centre. Q proportional so bands stay contiguous.
    for (int i = 0; i < activeBands; ++i)
    {
        const double f = (double) bandFreq[(size_t) i];
        const double Q = 4.0;   // moderately narrow, classic vocoder character
        modBP[(size_t) i].setBandpass (f, Q, sampleRate);
        carBP[(size_t) i].setBandpass (clampF (f * formantMul, sampleRate), Q, sampleRate);
    }
    for (auto& b : unvoicedHP) b.setHP (juce::jmin (6500.0, sampleRate * 0.42), sampleRate);

    // --- Sample loop ----------------------------------------------------------
    const float envInc = 1.0f / (float) juce::jmax (1, n);   // per-sample smoothing step

    for (int s = 0; s < n; ++s)
    {
        // Smooth wet / unvoiced controls toward targets (formant is applied via
        // the per-block carrier-band coefficients above, so it needs no per-sample
        // smoothing here).
        smWet      += (tgtWet - smWet) * envInc;
        smUnvoiced += (tgtUnvoiced - smUnvoiced) * envInc;

        // Modulator = mono sum of the input (the vocal).
        float mod = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) mod += buffer.getReadPointer (ch)[s];
        mod /= (float) juce::jmax (1, numCh);

        // Carrier = summed band-limited synth voices.
        float car = 0.0f;
        for (int v = 0; v < kMaxCarrier; ++v)
            if (voices[(size_t) v].active) car += voices[(size_t) v].osc.next (wave);
        if (nNotes > 0) car /= (float) juce::jmax (1, nNotes);   // normalise chord level

        // Vocoder core: for each band, track modulator amplitude, gate carrier.
        float voiced = 0.0f;
        for (int i = 0; i < activeBands; ++i)
        {
            const float mb = (float) modBP[(size_t) i].process (mod);
            const float env = bandEnv[(size_t) i].processRectified (mb);
            const float cb = (float) carBP[(size_t) i].process (car);
            voiced += cb * env;
        }
        voiced *= 2.0f;   // band-sum makeup (carrier bands are narrow)

        // Write output per channel. The unvoiced path high-passes the modulator
        // per channel so consonants (s / t / f) pass through directly.
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = buffer.getReadPointer (ch)[s];
            const float unv = (float) unvoicedHP[(size_t) ch].process (dry) * smUnvoiced;
            float y = voiced + unv;
            y = smWet * y + (1.0f - smWet) * dry;   // dry/wet blend (0 = pass the vocal)
            y *= outGain;
            y = juce::jlimit (-4.0f, 4.0f, y);       // hard safety clamp (never blow up)
            buffer.getWritePointer (ch)[s] = y;
        }
    }

    // We consumed MIDI to drive the carrier; do not pass notes downstream.
    midi.clear();
}

void SattariVocoderAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariVocoderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariVocoderAudioProcessor::createEditor()
{
    return new SattariVocoderAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariVocoderAudioProcessor();
}
