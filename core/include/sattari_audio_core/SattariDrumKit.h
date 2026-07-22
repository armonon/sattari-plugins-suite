#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// =============================================================================
//  SattariDrumKit — an all-synthesised analog-style drum voice engine.
// -----------------------------------------------------------------------------
//  One DrumVoice per kit slot, each a small self-contained synth in the 808/909
//  tradition: pitched sine bodies with fast pitch + amp envelopes for the drums,
//  filtered noise for the metals and claps. No samples, so it is a real instrument
//  out of the box, and pure-std so it is contract-tested like the rest of the core.
//
//  Per-voice controls are normalised 0..1 (tune, decay, tone, level); the voice maps
//  them to musically-sensible ranges per type. trigger() is allocation-free and
//  render() ADDS into the buffer so a kit can sum its active voices.
//
//  Realtime contract: prepare() may be called before audio; trigger()/render() never
//  allocate or lock.
// =============================================================================

namespace sattari::dsp
{

enum class DrumType
{
    Kick = 0, Snare, ClosedHat, OpenHat, Clap, Tom, Rim, Ride, kCount
};

struct DrumVoiceParams
{
    float tune  = 0.5f;   // pitch / brightness
    float decay = 0.5f;   // tail length
    float tone  = 0.5f;   // character (noise vs body / filter tilt)
    float level = 0.8f;   // output gain
};

class DrumVoice
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void setType (DrumType t) noexcept { type_ = t; }
    DrumType type() const noexcept { return type_; }

    void reset() noexcept
    {
        amp_ = pitchEnv_ = 0.0f;
        phase_ = phase2_ = 0.0f;
        hpZ_ = lpZ_ = bpZ1_ = bpZ2_ = 0.0f;
        clapTimer_ = clapBursts_ = 0;
        active_ = false;
    }

    bool active() const noexcept { return active_; }

    // Fire the voice. velocity 0..1 scales level; p sets the sound.
    void trigger (float velocity, const DrumVoiceParams& p) noexcept
    {
        p_ = p;
        vel_ = clamp01 (velocity);
        amp_ = 1.0f;
        pitchEnv_ = 1.0f;
        phase_ = phase2_ = 0.0f;

        // Per-type amp/pitch time constants (seconds), scaled by the Decay control.
        // Voices deactivate at ~-60 dB (kDeadLevel), i.e. after ~6.9 tau, so these keep
        // the audible tails drum-length (default ~0.3-1 s, long-decay 808/ride ~1.5-2 s).
        const float d = clamp01 (p.decay);   // 0..1
        switch (type_)
        {
            case DrumType::Kick:      ampTau_ = 0.03f  + 0.22f * d; pitchTau_ = 0.03f;  break;
            case DrumType::Snare:     ampTau_ = 0.04f  + 0.16f * d; pitchTau_ = 0.02f;  break;
            case DrumType::ClosedHat: ampTau_ = 0.010f + 0.05f * d; pitchTau_ = 0.01f;  break;
            case DrumType::OpenHat:   ampTau_ = 0.05f  + 0.20f * d; pitchTau_ = 0.01f;  break;
            case DrumType::Clap:      ampTau_ = 0.04f  + 0.12f * d; pitchTau_ = 0.01f;  break;
            case DrumType::Tom:       ampTau_ = 0.05f  + 0.22f * d; pitchTau_ = 0.06f;  break;
            case DrumType::Rim:       ampTau_ = 0.006f + 0.015f * d; pitchTau_ = 0.005f; break;
            case DrumType::Ride:      ampTau_ = 0.12f  + 0.40f * d; pitchTau_ = 0.01f;  break;
            default: ampTau_ = 0.1f; pitchTau_ = 0.02f; break;
        }
        ampCoeff_   = std::exp (-1.0f / (float) (sr_ * ampTau_));
        pitchCoeff_ = std::exp (-1.0f / (float) (sr_ * pitchTau_));
        clapBursts_ = (type_ == DrumType::Clap) ? 3 : 0;   // 3 quick re-attacks then the tail
        clapTimer_  = 0;
        active_ = true;
    }

    // Add this voice's mono output into out[0..n). Returns whether it is still ringing.
    bool render (float* out, int n) noexcept
    {
        if (! active_) return false;
        const float lvl = vel_ * (0.1f + 0.9f * clamp01 (p_.level));

        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            switch (type_)
            {
                case DrumType::Kick: case DrumType::Tom: s = renderPitched (i); break;
                case DrumType::Snare:                    s = renderSnare();     break;
                case DrumType::ClosedHat: case DrumType::OpenHat:
                case DrumType::Ride:                     s = renderMetal();     break;
                case DrumType::Clap:                     s = renderClap();      break;
                case DrumType::Rim:                      s = renderRim();       break;
                default: break;
            }

            amp_      *= ampCoeff_;
            pitchEnv_ *= pitchCoeff_;
            out[i] += s * lvl;

            if (amp_ < kDeadLevel) { active_ = false; break; }   // rung out (~-60 dB); rest is silence
        }
        return active_;
    }

private:
    static float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    float white() noexcept
    {
        rng_ = rng_ * 1664525u + 1013904223u;
        return ((float) ((rng_ >> 9) & 0x7fffff) / 4194304.0f) - 1.0f;   // -1..1
    }
    float sinp (float& ph, float hz) noexcept
    {
        const float out = std::sin (ph);
        ph += (float) (2.0 * M_PI * hz / sr_);
        if (ph > (float) (2.0 * M_PI)) ph -= (float) (2.0 * M_PI);
        return out;
    }
    // One-pole high pass (returns x minus its low-passed self).
    float hp (float x, float& z, float cutHz) noexcept
    {
        const float a = std::exp (-2.0f * (float) M_PI * cutHz / (float) sr_);
        z = (1.0f - a) * x + a * z;
        return x - z;
    }
    float lp (float x, float& z, float cutHz) noexcept
    {
        const float a = std::exp (-2.0f * (float) M_PI * cutHz / (float) sr_);
        z = (1.0f - a) * x + a * z;
        return z;
    }

    // Kick / Tom: sine with a downward pitch sweep + click.
    float renderPitched (int i) noexcept
    {
        const float semis = (p_.tune - 0.5f) * 24.0f;
        const float base  = (type_ == DrumType::Kick) ? 45.0f : 110.0f;
        const float top   = (type_ == DrumType::Kick) ? 120.0f : 90.0f;   // how far it sweeps up
        const float f = (base + top * pitchEnv_) * std::pow (2.0f, semis / 12.0f);
        float s = sinp (phase_, f) * amp_;
        // Click transient in the first ~5 ms, scaled by tone.
        if (i == 0 && amp_ > 0.99f) clickAmp_ = 0.6f * clamp01 (p_.tone);
        clickAmp_ *= clickCoeff_;
        s += white() * clickAmp_;
        return std::tanh (s * 1.2f);
    }

    // Snare: two tonal partials + band-passed noise, mixed by tone (snappy).
    float renderSnare() noexcept
    {
        const float t = std::pow (2.0f, (p_.tune - 0.5f) * 12.0f / 12.0f);
        const float body = 0.5f * (sinp (phase_, 180.0f * t) + sinp (phase2_, 330.0f * t)) * amp_;
        float noise = white() * amp_;
        noise = hp (noise, hpZ_, 1200.0f);
        noise = lp (noise, lpZ_, 6000.0f + 4000.0f * clamp01 (p_.tone));
        const float snap = 0.3f + 0.6f * clamp01 (p_.tone);
        return std::tanh ((1.0f - snap) * body + snap * noise * 1.5f);
    }

    // Closed/Open hat + Ride: steeply high-passed noise (two cascaded one-poles) so the
    // metals sit clearly above the snare's mid-band noise.
    float renderMetal() noexcept
    {
        const float cut = 6000.0f + 4000.0f * clamp01 (p_.tune);
        float n = white() * amp_;
        n = hp (n, hpZ_, cut);
        n = hp (n, bpZ1_, cut);   // second stage: steeper skirt, brighter
        return std::tanh (n * 1.6f);
    }

    // Clap: band-passed noise with a few quick re-attacks then a tail.
    float renderClap() noexcept
    {
        if (clapBursts_ > 0)
        {
            if (++clapTimer_ >= (int) (sr_ * 0.010f)) { amp_ = 1.0f; --clapBursts_; clapTimer_ = 0; }
        }
        float n = white() * amp_;
        n = hp (n, hpZ_, 900.0f);
        n = lp (n, lpZ_, 2500.0f + 2000.0f * clamp01 (p_.tone));
        return std::tanh (n * 1.4f);
    }

    // Rim: very short band-passed click.
    float renderRim() noexcept
    {
        const float tone = sinp (phase_, 1700.0f * std::pow (2.0f, (p_.tune - 0.5f))) * amp_;
        float n = white() * amp_ * 0.5f;
        n = hp (n, hpZ_, 1400.0f);
        return std::tanh ((tone + n) * 1.2f);
    }

    double sr_ = 44100.0;
    DrumType type_ = DrumType::Kick;
    DrumVoiceParams p_ {};
    float vel_ = 1.0f;

    float amp_ = 0.0f, ampCoeff_ = 0.99f, ampTau_ = 0.1f;
    float pitchEnv_ = 0.0f, pitchCoeff_ = 0.99f, pitchTau_ = 0.02f;
    float phase_ = 0.0f, phase2_ = 0.0f;
    float hpZ_ = 0.0f, lpZ_ = 0.0f, bpZ1_ = 0.0f, bpZ2_ = 0.0f;
    float clickAmp_ = 0.0f;
    const float clickCoeff_ = 0.985f;
    int clapTimer_ = 0, clapBursts_ = 0;
    std::uint32_t rng_ = 0x9e3779b9u;
    bool active_ = false;
    static constexpr float kDeadLevel = 1.0e-3f;   // ~-60 dB: voice frees itself here
};

// A fixed kit: one voice per lane, summed. Lanes are assigned a default type but a
// host can re-point any lane at any DrumType (e.g. a second tom).
template <int kLanes>
class DrumKit
{
public:
    void prepare (double sampleRate) noexcept
    {
        for (auto& v : voices_) v.prepare (sampleRate);
    }
    void reset() noexcept { for (auto& v : voices_) v.reset(); }

    void setLaneType (int lane, DrumType t) noexcept
    {
        if (lane >= 0 && lane < kLanes) voices_[(size_t) lane].setType (t);
    }
    void trigger (int lane, float velocity, const DrumVoiceParams& p) noexcept
    {
        if (lane >= 0 && lane < kLanes) voices_[(size_t) lane].trigger (velocity, p);
    }
    // Sum all active voices into out[0..n) (mono). Returns count still ringing.
    int render (float* out, int n) noexcept
    {
        int live = 0;
        for (auto& v : voices_) if (v.render (out, n)) ++live;
        return live;
    }

private:
    std::array<DrumVoice, (size_t) kLanes> voices_;
};

} // namespace sattari::dsp
