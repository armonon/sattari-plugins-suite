//  Sattari Audio Suite — channel vocoder bank.
//
//  Classic N-band channel vocoder: a MODULATOR (the voice) shapes a CARRIER (a synth
//  tone). Each band measures the modulator's envelope and multiplies it onto the same
//  band of the carrier, so the carrier "speaks" in the voice's spectral shape — the
//  talkbox / robot-choir sound, without a tube in your mouth. In VoxSynth this is what
//  turns the continuous-pitch synth into a vocoder voice.
//
//  Log-spaced bandpass bands (RBJ biquads), one envelope follower per band. Pure-std,
//  RT-safe after prepare().
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sattari::dsp
{

class VocoderBank
{
public:
    void prepare (double sampleRate, int numBands = 16, double loHz = 120.0, double hiHz = 8000.0)
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        n_  = std::max (4, numBands);
        mod_.assign ((size_t) n_, {}); car_.assign ((size_t) n_, {}); env_.assign ((size_t) n_, 0.0f);
        // Log-spaced centre frequencies; Q ~ centre/bandwidth for adjacent overlap.
        for (int b = 0; b < n_; ++b)
        {
            const double f = loHz * std::pow (hiHz / loHz, (double) b / (n_ - 1));
            const double q = 4.0;
            mod_[(size_t) b].bandpass (sr_, f, q);
            car_[(size_t) b].bandpass (sr_, f, q);
        }
        const float envMs = 12.0f;
        envCoef_ = 1.0f - std::exp (-1.0f / (0.001f * envMs * (float) sr_));
    }

    void reset()
    {
        for (auto& b : mod_) b.reset();
        for (auto& b : car_) b.reset();
        std::fill (env_.begin(), env_.end(), 0.0f);
    }

    // out[i] = amount-blend between the carrier and the vocoded (voice-shaped) carrier.
    // Safe when out aliases carrier. modulator = the voice, carrier = the synth.
    void process (const float* carrier, const float* modulator, float* out, int nSamples, float amount = 1.0f)
    {
        const float a = std::clamp (amount, 0.0f, 1.0f);
        for (int i = 0; i < nSamples; ++i)
        {
            const float c = carrier[i], m = modulator[i];
            float voc = 0.0f;
            for (int b = 0; b < n_; ++b)
            {
                const float mb = mod_[(size_t) b].process (m);
                env_[(size_t) b] += envCoef_ * (std::abs (mb) - env_[(size_t) b]);
                voc += car_[(size_t) b].process (c) * env_[(size_t) b];
            }
            out[i] = (1.0f - a) * c + a * (voc * 2.0f);   // 2x makeup for band-sum loss
        }
    }

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void bandpass (double sr, double f0, double Q)
        {
            const double w0 = 2.0 * 3.14159265358979323846 * f0 / sr;
            const double al = std::sin (w0) / (2.0 * Q), cw = std::cos (w0), a0 = 1.0 + al;
            b0 = (float) (al / a0); b1 = 0.0f; b2 = (float) (-al / a0);
            a1 = (float) (-2.0 * cw / a0); a2 = (float) ((1.0 - al) / a0);
        }
        void reset() { z1 = z2 = 0; }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    double sr_ = 48000.0;
    int    n_ = 16;
    float  envCoef_ = 0.1f;
    std::vector<Biquad> mod_, car_;
    std::vector<float>  env_;
};

} // namespace sattari::dsp
