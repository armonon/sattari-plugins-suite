#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>   // ScaleId + scaleIntervals + midiToHz

#include <array>
#include <cmath>

// =============================================================================
//  VocoderModule — Sattari Vocoder's DSP, extracted so the standalone plugin and
//  a Create rack slot run the SAME vocoder.
//
//  Classic carrier-synthesis vocoder: the input vocal is the MODULATOR, and a
//  built-in polyBLEP synth is the CARRIER. Per band, an envelope follower tracks
//  the modulator's amplitude and gates the matching carrier band, so the vocal's
//  spectral shape is imposed on the synth.
//
//  Two things make it musical rather than a lab exercise:
//    * The carrier chord is real. Held MIDI notes WIN — if the player is
//      fingering a chord, that is the intent — and otherwise the module
//      auto-voices an in-key chord from the KEY bus or the manual key controls.
//      This is why the host must be `aumf`: without MIDI input the carrier could
//      only ever auto-voice.
//    * Consonants survive. A vocoder band bank cannot reproduce unvoiced sounds
//      (s / t / f) because they have no harmonic structure to gate, so the
//      UNVOICED path high-passes the raw modulator straight to the output.
//
//  Formant Shift moves the CARRIER bank's centres only, so the spectral envelope
//  slides without changing the pitch the carrier plays.
// =============================================================================

namespace sattari::dsp
{

class VocoderModule final : public SattariModule
{
public:
    static constexpr int kMaxBands   = 32;
    static constexpr int kMaxCarrier = 6;   // carrier voices (chord) + unisons

    ~VocoderModule() override { sessionReader.stop(); }

    static juce::StringArray rootChoices()
    { return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
    static juce::StringArray scaleChoices()
    { return { "Chromatic", "Major", "Minor", "Penta Maj", "Penta Min", "Dorian", "Mixolydian", "Harm Minor" }; }
    static juce::StringArray waveChoices()    { return { "Saw", "Pulse" }; }
    static juce::StringArray voicingChoices() { return { "Root", "Octaves", "Fifths", "Triad", "Seventh" }; }

    struct KeyState { bool following = false; int root = 0; int scale = 1; };

    const char* moduleId()    const noexcept override { return "vocoder"; }
    const char* displayName() const noexcept override { return "Vocoder"; }

    VocoderModule() { heldMidi.fill (-1); }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using R  = juce::NormalisableRange<float>;

        out.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID (prefix + "bands", 1), "Bands", 8, 32, 16));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "wave", 1), "Carrier Wave", waveChoices(), 0));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "voicing", 1), "Voicing", voicingChoices(), 1));
        out.push_back (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID (prefix + "octave", 1), "Carrier Octave", -2, 2, 0));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "formant", 1), "Formant Shift",
            R (-12.0f, 12.0f, 0.1f), 0.0f));                 // semitones of band shift
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "unvoiced", 1), "Unvoiced / Sibilance",
            R (0.0f, 1.0f, 0.01f), 0.30f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "attack", 1), "Follower Attack",
            R (0.5f, 50.0f, 0.1f, 0.4f), 5.0f));             // ms
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "release", 1), "Follower Release",
            R (5.0f, 400.0f, 0.1f, 0.4f), 60.0f));           // ms
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "mix", 1), "Dry / Wet",
            R (0.0f, 1.0f, 0.01f), 1.0f));                   // 0 = dry (vocal), 1 = wet
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "output", 1), "Output",
            R (-12.0f, 12.0f, 0.1f), 0.0f));                 // dB

        // Key control (matches the shared KeyButton convention across the suite).
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID (prefix + "followKey", 1), "Follow Song Key", false));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "root", 1), "Root", rootChoices(), 0));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "scale", 1), "Scale", scaleChoices(), 2));   // Minor
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        computeBandFrequencies ((int) std::round (p ("bands")));
        for (auto& e : bandEnv) e.prepare (sr, 5.0f, 60.0f);
        sessionReader.start();   // poll the cross-plugin key bus off the audio thread
        reset();
    }

    void reset() override
    {
        for (auto& b : modBP) b.reset();
        for (auto& b : carBP) b.reset();
        for (auto& e : bandEnv) e.reset (0.0f);
        for (auto& b : unvoicedHP) b.reset();
        for (auto& v : voices) { v.osc.reset(); v.active = false; v.note = -1; v.freq = 0.0f; }
        heldMidi.fill (-1);
        heldMidiCount = 0;
        smWet      = state != nullptr ? p ("mix") : 1.0f;
        smUnvoiced = state != nullptr ? p ("unvoiced") : 0.3f;
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        // --- Absorb incoming MIDI (host player fingering the carrier chord) ---
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

        // --- Per-block parameter read + coefficient update --------------------
        const int reqBands = juce::jlimit (8, kMaxBands, (int) std::round (p ("bands")));
        if (reqBands != activeBands)
        {
            computeBandFrequencies (reqBands);
            for (int i = 0; i < activeBands; ++i) { modBP[(size_t) i].reset(); carBP[(size_t) i].reset(); }
        }

        const int   wave      = juce::jlimit (0, 1, (int) std::round (p ("wave")));
        const int   voicing   = juce::jlimit (0, 4, (int) std::round (p ("voicing")));
        const int   octave    = juce::jlimit (-2, 2, (int) std::round (p ("octave")));
        const float attackMs  = p ("attack");
        const float releaseMs = p ("release");
        const float outGain   = juce::Decibels::decibelsToGain (p ("output"));
        const float tgtWet      = juce::jlimit (0.0f, 1.0f, p ("mix"));
        const float tgtUnvoiced = juce::jlimit (0.0f, 1.0f, p ("unvoiced"));
        const float formantSemis = p ("formant");

        for (auto& e : bandEnv) e.setTimes (attackMs, releaseMs);

        int root; ScaleId id; resolveKey (root, id);

        // formant applies a per-band centre shift on the CARRIER bank (moves the
        // spectral envelope without changing the pitch the carrier plays).
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
                    voices[(size_t) v].freq = midiToHz ((float) note);
                    voices[(size_t) v].active = true;
                    // keep phase running to avoid clicks
                }
                voices[(size_t) v].osc.setFreq (voices[(size_t) v].freq, sr);
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
            modBP[(size_t) i].setBandpass (f, Q, sr);
            carBP[(size_t) i].setBandpass (clampF (f * formantMul, sr), Q, sr);
        }
        for (auto& b : unvoicedHP) b.setHP (juce::jmin (6500.0, sr * 0.42), sr);

        // --- Sample loop ------------------------------------------------------
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

    KeyState currentKeyState() const
    {
        KeyState k;
        int root; ScaleId id; resolveKey (root, id);
        k.root = root; k.scale = (int) id;
        k.following = p ("followKey") > 0.5f;
        return k;
    }

private:
    static double clampF (double f, double sampleRate) noexcept
    { return juce::jlimit (20.0, sampleRate * 0.45, f); }

    // --- Biquad — bandpass (constant skirt gain) + high-pass, RBJ cookbook ---
    struct Biquad
    {
        double b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
        void reset() noexcept { x1=x2=y1=y2=0; }
        double process (double x) noexcept
        { const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2; x2=x1;x1=x;y2=y1;y1=y; return y; }

        void setBandpass (double f, double Q, double sampleRate) noexcept
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sampleRate) / sampleRate;
            const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * juce::jmax (0.1, Q));
            const double a0 = 1.0 + alpha;
            b0 = alpha / a0; b1 = 0.0; b2 = -alpha / a0; a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }

        void setHP (double f, double sampleRate) noexcept
        {
            const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sampleRate) / sampleRate;
            const double c = std::cos (w0), s = std::sin (w0), alpha = s / (2.0 * 0.707);
            const double a0 = 1.0 + alpha;
            b0 = ((1.0 + c) * 0.5) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) * 0.5) / a0;
            a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
        }
    };

    // --- BlepOsc — band-limited saw / pulse via polyBLEP --------------------
    struct BlepOsc
    {
        double phase = 0.0, inc = 0.0;
        void setFreq (double hz, double sampleRate) noexcept { inc = (sampleRate > 0.0) ? hz / sampleRate : 0.0; }
        void reset() noexcept { phase = 0.0; }

        // wave: 0 = saw, 1 = pulse (50%). Returns roughly [-1, 1].
        float next (int wave) noexcept
        {
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;

            const double dt = inc;
            auto poly = [dt] (double t) noexcept -> double
            {
                if (dt <= 0.0) return 0.0;
                if (t < dt)            { t /= dt;            return t + t - t * t - 1.0; }
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
    };

    // Log-spaced band centres over the classic vocoder range (~110 Hz .. 8 kHz).
    void computeBandFrequencies (int bands) noexcept
    {
        bands = juce::jlimit (8, kMaxBands, bands);
        const double lo = 110.0, hi = juce::jmin (8000.0, sr * 0.45);
        const double ratio = std::pow (hi / lo, 1.0 / juce::jmax (1, bands - 1));
        double f = lo;
        for (int i = 0; i < bands; ++i) { bandFreq[(size_t) i] = (float) f; f *= ratio; }
        activeBands = bands;
    }

    void resolveKey (int& root, ScaleId& id) const
    {
        root = (int) std::round (p ("root"));
        id   = (ScaleId) (int) std::round (p ("scale"));
        if (p ("followKey") > 0.5f)
            if (auto s = sessionReader.current(); s.valid)
            {
                root = juce::jlimit (0, 11, s.root);
                id   = (s.scale == 1) ? ScaleId::Minor : ScaleId::Major;   // bus: 0=maj,1=min
            }
    }

    // Build carrier MIDI notes. If the host is sending held MIDI notes, those win
    // (the player is fingering the chord). Otherwise auto-voice an in-key chord.
    int carrierNotes (int* out, int maxOut, int root, ScaleId id, int voicing, int octave) const
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
        const int* iv = scaleIntervals (id, n);
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
            case 3: add (degree (0)); add (degree (2)); add (degree (4)); break;                    // Triad
            case 4: add (degree (0)); add (degree (2)); add (degree (4)); add (degree (6)); break;  // Seventh
            default: add (base); break;
        }
        return c;
    }

    float p (const char* id) const noexcept
    {
        auto* v = state->getRawParameterValue (pfx + id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    mutable sattari::xplugin::SessionReader sessionReader;

    // Per-band filters (mono modulator detector + carrier shaping bank).
    std::array<Biquad, kMaxBands> modBP;
    std::array<Biquad, kMaxBands> carBP;
    std::array<EnvelopeFollower, kMaxBands> bandEnv;
    std::array<float, kMaxBands> bandFreq { {} };
    int activeBands = 16;

    // Carrier synth voices — fixed-size for RT safety.
    struct Voice { BlepOsc osc; bool active = false; int note = -1; float freq = 0.0f; };
    std::array<Voice, kMaxCarrier> voices;

    // Held MIDI notes (from the host), newest-priority; -1 = empty.
    std::array<int, 16> heldMidi { {} };
    int heldMidiCount = 0;

    // Unvoiced high-pass path (per channel).
    std::array<Biquad, 2> unvoicedHP;

    // Smoothed control values to avoid zipper on param moves.
    float smWet = 1.0f, smUnvoiced = 0.3f;
};

} // namespace sattari::dsp
