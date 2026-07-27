#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>
#include <sattari_audio_core/SattariPitchDetector.h>   // ScaleId + scale-snap helpers
#include <sattari_audio_core/SattariPsolaShifter.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  ShimmerModule — Sattari Shimmer's DSP, extracted so the standalone plugin and
//  a Create rack slot run the SAME shimmer reverb.
//
//  A reverb tank with a pitch shifter INSIDE its feedback path, so the tail
//  blooms upward into a crystalline pad:
//
//    dry -> [pre-delay] -> tank input += shimmer * shifted(previous block's wet)
//                            -> Freeverb (pure wet) -> PSOLA up-shift -> damp
//
//  Everything about this module is a stability argument. A pitch shifter in a
//  reverb feedback loop is trivially capable of running away, so the round-trip
//  gain is held below 1 for EVERY parameter combination by four independent
//  bounds, none of which is redundant:
//    1. roomSize is capped at 0.80 (Freeverb comb feedback = roomSize*0.28+0.7,
//       so <= 0.938 — always dissipative, and freeze is deliberately unreachable);
//    2. the shimmer injection is throttled AS SIZE RISES, staying well under the
//       reciprocal of the tank's worst-case wet gain;
//    3. the feedback low-pass has a real coefficient FLOOR, so every pass sheds
//       high end no matter where the "damping" control sits — it is a stability
//       device wearing a tone control's clothes;
//    4. the loop saturator is a tanh, whose small-signal gain is <= 1; a hard
//       clip would instead support a self-sustaining limit cycle.
//  Measured stable to silence at shimmer = 0.95, size = 1.0.
//
//  Key Snap quantises the shimmer interval to in-key scale tones from the KEY
//  bus, keeping it inside a musical +3..+14 window so the bloom always rises.
// =============================================================================

namespace sattari::dsp
{

class ShimmerModule final : public SattariModule
{
public:
    ~ShimmerModule() override { sessionReader.stop(); }

    static juce::StringArray intervalChoices() { return { "+12 (octave)", "+7 (fifth)", "+5 (fourth)" }; }
    static juce::StringArray rootChoices()     { return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
    static juce::StringArray scaleChoices()    { return { "major", "minor" }; }

    struct KeyState { bool snapping = false; int root = 0; int scale = 1; };

    const char* moduleId()    const noexcept override { return "shimmer"; }
    const char* displayName() const noexcept override { return "Shimmer"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using R  = juce::NormalisableRange<float>;
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "size", 1),     "Size / Decay",   R (0.0f, 1.0f, 0.01f), 0.75f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "shimmer", 1),  "Shimmer",        R (0.0f, 0.95f, 0.01f), 0.45f));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "interval", 1), "Interval", intervalChoices(), 0));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "damp", 1),     "Damping / Tone", R (0.0f, 1.0f, 0.01f), 0.35f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "predelay", 1), "Pre-Delay",      R (0.0f, 200.0f, 1.0f), 20.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "width", 1),    "Width",          R (0.0f, 1.0f, 0.01f), 1.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "mix", 1),      "Mix",            R (0.0f, 1.0f, 0.01f), 0.35f));

        // Key Snap: quantize the shimmer interval to in-key scale tones from the bus.
        out.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (prefix + "keysnap", 1), "Key Snap", false));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (prefix + "root", 1),  "Key Root",  rootChoices(), 0));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (prefix + "scale", 1), "Key Scale", scaleChoices(), 0));
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        const int block = juce::jmax (1, maxBlockSize);

        reverb.setSampleRate (sr);

        dryScratch.setSize   (2, block);
        tankIn.setSize       (2, block);
        shiftScratch.setSize (2, block);
        fbCarry.setSize      (2, block);

        // Pre-delay line: up to 200 ms + a block of slack.
        preLen = (int) std::ceil (sr * 0.2) + block + 8;
        for (auto& d : preBuf) d.assign ((size_t) preLen, 0.0f);

        // Formant-preserving shifter per channel, tuned for the musical vocal/pad
        // range the shimmer octave lands in.
        psola.assign (2, PsolaShifter());
        for (auto& s : psola) s.prepare (sr, 60.0, 1200.0);

        sessionReader.start();
        reset();
    }

    void reset() override
    {
        reverb.reset();
        for (auto& d : preBuf) std::fill (d.begin(), d.end(), 0.0f);
        preWrite = 0;
        dryScratch.clear();
        tankIn.clear();
        shiftScratch.clear();
        fbCarry.clear();
        toneState = { { 0.0f, 0.0f } };
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = juce::jmin (buffer.getNumSamples(), fbCarry.getNumSamples());
        if (numCh <= 0 || n <= 0) return;

        const float size    = p ("size");
        const float shimmer = p ("shimmer");
        const float damp    = p ("damp");

        // Bound (2) of the stability argument — see the header.
        const float shimmerInject = shimmer * (0.45f - 0.33f * size);   // 0.45 (small) .. 0.12 (large)
        const float width   = p ("width");
        const float mix     = p ("mix");
        const int   preSamp = juce::jlimit (0, preLen - 4,
                                 (int) std::round (p ("predelay") * 0.001 * sr));

        // --- Effective shimmer interval (optionally key-snapped) -------------
        const int baseSemis = baseSemisFor ((int) std::round (p ("interval")));
        int keyRoot; ScaleId keyId; resolveKey (keyRoot, keyId);
        const int semis = (p ("keysnap") > 0.5f)
                            ? effectiveShimmerSemis (baseSemis, keyRoot, keyId)
                            : baseSemis;
        liveInterval.store (semis, std::memory_order_relaxed);
        const double pitchScale = std::pow (2.0, (double) semis / 12.0);   // upward transpose

        // --- Save the pure dry for our own mix, and build the tank input -----
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

        // --- Reverb tank (Freeverb), taken as PURE wet ----------------------
        // juce::Reverb scales dry x2 / wet x3 internally, so its dry/wet is not a
        // clean crossfade. Take dryLevel 0 and wetLevel 1/3 (=~unity wet), then do
        // our OWN dry/wet mix below so mix=0 is an exact pass-through.
        // roomSize cap is bound (1) of the stability argument.
        juce::Reverb::Parameters rp;
        rp.roomSize   = juce::jlimit (0.0f, 0.80f, 0.28f + 0.52f * size);
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

        // --- Build next block's shimmer feedback: shift the wet tail up, damp it
        const double period = sr / 220.0;   // mid reference period; PSOLA clamps internally
        // Bound (3): the low-pass coefficient FLOOR, so the loop always sheds high
        // end every pass regardless of where "damping" sits.
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
                st += toneCoeff * (sh[i] - st);           // one-pole LP (damping/tone + stability)
                if (! std::isfinite (st)) st = 0.0f;      // safety against denormal/NaN creep
                // Bound (4): tanh, not a hard clip — small-signal gain <= 1, so the
                // loop cannot support a self-sustaining limit cycle and still winds
                // down to silence after the input stops.
                fbOut[i] = std::tanh (st);
            }
            toneState[(size_t) ch] = st;
        }
        if (numCh == 1) fbCarry.copyFrom (1, 0, fbCarry, 0, 0, n);   // keep ch1 sane in mono

        // --- Final dry/wet mix (mix=0 -> exact pass-through) -----------------
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

    // Quantize the base shimmer interval so the transposed pitch class is in-key.
    // The bloom is transposition-invariant across octaves, so we snap the interval's
    // pitch-class offset to the nearest scale tone (relative to the key root) and
    // keep it inside a musical +3..+14 semitone window so the shimmer still rises.
    int effectiveShimmerSemis (int baseSemis, int keyRoot, ScaleId id) const
    {
        if (id == ScaleId::ChromaticOff)
            return baseSemis;

        int n = 0;
        const int* iv = scaleIntervals (id, n);
        if (iv == nullptr || n <= 0)
            return baseSemis;

        // Snap a reference note (root + baseSemis) to the scale, take the resulting
        // interval. snapMidiToScale rounds to the nearest in-key pitch class.
        const int ref     = 60 + keyRoot + baseSemis;             // C5-ish reference + interval
        const int snapped = snapMidiToScale (ref, keyRoot, id);
        int semis = baseSemis + (snapped - ref);

        // Keep it an upward, musical shimmer (never fold to unison or below a minor 3rd).
        if (semis < 3)  semis += 12;
        if (semis > 14) semis -= 12;
        return juce::jlimit (3, 19, semis);
    }

    KeyState currentKeyState() const
    {
        KeyState k;
        int root; ScaleId id; resolveKey (root, id);
        k.root     = root;
        k.scale    = (id == ScaleId::Minor) ? 1 : 0;
        k.snapping = p ("keysnap") > 0.5f;
        return k;
    }

    int currentInterval() const noexcept { return liveInterval.load (std::memory_order_relaxed); }

private:
    static int baseSemisFor (int intervalIndex) noexcept
    {
        switch (intervalIndex)
        {
            case 1:  return 7;    // perfect fifth
            case 2:  return 5;    // perfect fourth
            default: return 12;   // octave
        }
    }

    void resolveKey (int& root, ScaleId& id) const
    {
        root = (int) std::round (p ("root"));
        id   = ((int) std::round (p ("scale")) == 1) ? ScaleId::Minor : ScaleId::Major;

        // When Key Snap is on, follow the KEY bus if a fresh key is being published.
        if (p ("keysnap") > 0.5f)
            if (auto s = sessionReader.current(); s.valid)
            {
                root = juce::jlimit (0, 11, s.root);
                id   = (s.scale == 1) ? ScaleId::Minor : ScaleId::Major;   // bus: 0=maj,1=min
            }
    }

    float p (const char* id) const noexcept
    {
        auto* v = state->getRawParameterValue (pfx + id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    mutable sattari::xplugin::SessionReader sessionReader;   // consumes the KEY bus

    // Reverb tank, taken as pure wet (Space pattern; own dry/wet mix).
    juce::Reverb reverb;
    juce::AudioBuffer<float> dryScratch;   // dry(+pre-delay) held for our mix
    juce::AudioBuffer<float> tankIn;       // pre-delayed input + shimmer injection
    juce::AudioBuffer<float> shiftScratch; // shifted wet tail before re-injection

    // Pre-delay line (per channel).
    std::array<std::vector<float>, 2> preBuf;
    int preLen = 0, preWrite = 0;

    // Pitch shifter in the feedback path (formant-preserving, per channel).
    std::vector<PsolaShifter> psola;

    // Damping (tone) one-pole LP state on the shimmer feedback, per channel.
    std::array<float, 2> toneState { { 0.0f, 0.0f } };

    // Held shifted tail from the previous block, re-injected into this block's
    // tank input (closes the feedback loop without allocating).
    juce::AudioBuffer<float> fbCarry;

    // Live readout for the UI.
    std::atomic<int> liveInterval { 0 };   // effective interval in semitones
};

} // namespace sattari::dsp
