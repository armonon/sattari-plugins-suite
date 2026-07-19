//  Sattari Audio Suite — VoxSynth continuous-pitch synth engine.
//
//  The "synth mode" voice of VoxSynth: the analyzer (SattariVoxAnalyzer) describes the
//  voice per hop, and this turns that description into sound WITHOUT a MIDI note grid.
//  The point is expressive immediacy —
//     - oscillators track the detected pitch CONTINUOUSLY (portamento, not stepped),
//     - the VCA follows the voice's amplitude ENVELOPE directly (no ADSR — your mouth
//       is the envelope), so every dynamic nuance carries through,
//     - the filter cutoff tracks the voice's spectral centroid ("ooh" dark, "eee"
//       bright),
//     - a Quantize control blends continuous pitch -> scale-snapped (for the bus key).
//  That amplitude-IS-the-envelope mapping is what makes it feel alive where MIDI-into-
//  a-synth feels dead.
//
//  Driven at hop rate via setTarget(); render() smooths every control per-sample.
//  Two PolyBLEP oscillators + a TPT state-variable filter. Pure-std, RT-safe.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace sattari::dsp
{

class VoxSynthEngine
{
public:
    enum Wave { Sine = 0, Saw, Square, Triangle };

    struct Params
    {
        int   osc1Wave = Saw, osc2Wave = Saw;
        int   osc2Octave = 0;         // -2..+2
        float osc2Detune = 7.0f;      // cents
        float mix = 0.5f;             // 0 = osc1 only, 1 = osc2 only
        float glideMs = 12.0f;        // portamento time
        float filterBaseHz = 300.0f;  // cutoff floor
        float filterAmount = 1.0f;    // 0..1 centroid -> cutoff
        float filterReso = 0.2f;      // 0..1
        float quantize = 0.0f;        // 0 continuous .. 1 fully scale-snapped
        float tuningRefHz = 440.0f;
        float level = 0.9f;
    };

    void prepare (double sampleRate)
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        setScaleChromatic();
        reset();
    }

    void reset()
    {
        ph1_ = ph2_ = 0.0f;
        curFreq_ = tgtFreq_ = 0.0f;
        curAmp_ = tgtAmp_ = 0.0f;
        curCut_ = tgtCut_ = p_.filterBaseHz;
        ic1_ = ic2_ = 0.0f; triZ_ = 0.0f;
        voiced_ = false;
    }

    void setParams (const Params& p) noexcept { p_ = p; }

    // 12 pitch classes: true = in scale. tonic 0..11. Used only when quantize > 0.
    void setScale (const bool inScale[12]) noexcept
    {
        for (int i = 0; i < 12; ++i) scale_[(size_t) i] = inScale[i];
    }
    void setScaleChromatic() noexcept { for (auto& b : scale_) b = true; }

    // Push a fresh analysis frame. Targets are ramped toward in render().
    void setTarget (float freqHz, float amplitude, float centroidHz, bool voiced) noexcept
    {
        voiced_ = voiced;
        tgtAmp_ = voiced ? std::clamp (amplitude, 0.0f, 1.0f) : 0.0f;
        if (voiced && freqHz > 0.0f)
        {
            tgtFreq_ = maybeQuantise (freqHz);
            if (curFreq_ <= 0.0f) curFreq_ = tgtFreq_;   // snap on first note (no glide from 0)
        }
        // Cutoff = base + centroid*amount, clamped below Nyquist.
        const float cut = p_.filterBaseHz + p_.filterAmount * centroidHz;
        tgtCut_ = std::clamp (cut, 40.0f, (float) (sr_ * 0.45));
    }

    // Render n mono samples. Safe when the target is unchanged between calls.
    void render (float* out, int n) noexcept
    {
        const float glideC = onePole (p_.glideMs);
        const float ampC   = onePole (voiced_ ? 4.0f : 30.0f);   // fast up, slower release
        const float cutC   = onePole (8.0f);
        const float k = 2.0f - 2.0f * std::clamp (p_.filterReso, 0.0f, 0.98f);
        const float det = std::pow (2.0f, p_.osc2Detune / 1200.0f) * std::pow (2.0f, (float) p_.osc2Octave);

        for (int i = 0; i < n; ++i)
        {
            curFreq_ += glideC * (tgtFreq_ - curFreq_);
            curAmp_  += ampC   * (tgtAmp_  - curAmp_);
            curCut_  += cutC   * (tgtCut_  - curCut_);

            const float f1 = std::max (1.0f, curFreq_);
            const float f2 = std::max (1.0f, curFreq_ * det);
            const float dt1 = f1 / (float) sr_, dt2 = f2 / (float) sr_;
            const float o1 = osc (p_.osc1Wave, ph1_, dt1, triZ1_);
            const float o2 = osc (p_.osc2Wave, ph2_, dt2, triZ2_);
            ph1_ += dt1; if (ph1_ >= 1.0f) ph1_ -= 1.0f;
            ph2_ += dt2; if (ph2_ >= 1.0f) ph2_ -= 1.0f;

            float s = (1.0f - p_.mix) * o1 + p_.mix * o2;
            s = svfLowpass (s, curCut_, k);
            out[i] = s * curAmp_ * p_.level;
        }
    }

private:
    float onePole (float ms) const noexcept
    {
        const float t = std::max (0.1f, ms) * 0.001f;
        return 1.0f - std::exp (-1.0f / (t * (float) sr_));
    }

    float maybeQuantise (float freqHz) const noexcept
    {
        if (p_.quantize <= 0.0f) return freqHz;
        const float midi = 69.0f + 12.0f * std::log2 (freqHz / p_.tuningRefHz);
        // nearest in-scale semitone
        int best = (int) std::lround (midi); float bestD = 1e9f;
        for (int d = -6; d <= 6; ++d)
        {
            const int m = (int) std::lround (midi) + d;
            if (! scale_[(size_t) (((m % 12) + 12) % 12)]) continue;
            const float dist = std::abs (midi - m);
            if (dist < bestD) { bestD = dist; best = m; }
        }
        const float snappedMidi = midi + p_.quantize * (best - midi);
        return p_.tuningRefHz * std::pow (2.0f, (snappedMidi - 69.0f) / 12.0f);
    }

    static float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt;        return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    static float osc (int wave, float ph, float dt, float& triZ) noexcept
    {
        switch (wave)
        {
            case Saw:      return (2.0f * ph - 1.0f) - polyBlep (ph, dt);
            case Square:
            {
                float s = ph < 0.5f ? 1.0f : -1.0f;
                s += polyBlep (ph, dt);
                s -= polyBlep (std::fmod (ph + 0.5f, 1.0f), dt);
                return s;
            }
            case Triangle:
            {
                float sq = ph < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep (ph, dt); sq -= polyBlep (std::fmod (ph + 0.5f, 1.0f), dt);
                triZ = (1.0f - 0.001f) * triZ + dt * 4.0f * sq;   // leaky integrator
                return triZ;
            }
            default:       return std::sin (6.28318530718f * ph);
        }
    }

    // TPT state-variable lowpass (Zavalishin). Stable at high cutoff/reso.
    float svfLowpass (float in, float cutHz, float k) noexcept
    {
        const float g = std::tan (3.14159265359f * std::min (cutHz / (float) sr_, 0.49f));
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float v3 = in - ic2_;
        const float v1 = a1 * ic1_ + a2 * v3;
        const float v2 = ic2_ + g * v1;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        return v2;
    }

    double sr_ = 48000.0;
    Params p_;
    std::array<bool, 12> scale_ { };
    float ph1_ = 0, ph2_ = 0, triZ1_ = 0, triZ2_ = 0, triZ_ = 0;
    float curFreq_ = 0, tgtFreq_ = 0, curAmp_ = 0, tgtAmp_ = 0, curCut_ = 300, tgtCut_ = 300;
    float ic1_ = 0, ic2_ = 0;
    bool  voiced_ = false;
};

} // namespace sattari::dsp
