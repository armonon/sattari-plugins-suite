// =============================================================================
//  Sattari — KeyAnalysisEngine: ONE key detector for the whole suite
// -----------------------------------------------------------------------------
//  WHY THIS EXISTS
//
//  Before this header there were two key intelligences. Sattari Key loaded
//  keynet_spec.onnx; Compass — the session CONDUCTOR, whose published answer
//  outranks Sattari Key's on the bus — ran the chroma path with the Aarden
//  profile and no model at all. Measured 2026-08-11 on 500 GiantSteps and 837
//  GTZAN tracks, the conductor's profile placed last or near-last on both:
//
//      profile                GiantSteps MIREX/exact   GTZAN MIREX/exact
//      Aarden (what Compass ran)   47.34 / 39.8%          46.30 / 38.8%
//      Krumhansl                   51.90 / 38.8%          52.22 / 41.6%
//      Sha'ath                     56.56 / 45.2%          53.36 / 43.6%
//
//  The obvious fix — flip SATTARI_KEY_PROFILE — could not be applied, because
//  it is a GLOBAL compile-time #define. Changing it changes the detector for
//  every consumer at once, including Auto Pitch's AutoKey fallback, which is
//  frozen for release-candidate qualification. A "one-line free win" would have
//  silently invalidated a frozen candidate and every measurement taken against
//  it.
//
//  That is the real defect: the profile is suite-wide collateral rather than a
//  per-consumer decision. This engine makes it configuration.
//
//  WHAT THIS IS NOT
//
//  Not a plugin-to-plugin dependency. Compass does not ask Sattari Key for an
//  answer; both consume the same engine. And not a second copy of the key
//  intelligence: the chroma path still calls detail::detectKeyWithProfiles()
//  and the neural path still calls KeyNetSpec, unchanged. Nothing is
//  reimplemented here, only routed.
//
//  BEHAVIOUR IS PRESERVED BY DEFAULT. KeyAnalysisConfig{} reproduces exactly
//  what detectKey() does today — Aarden profile, chroma only, no model — so a
//  caller that adopts this engine without changing its config gets byte-
//  identical results. Auto Pitch can migrate after its freeze lifts without any
//  behavioural change at all, then opt into a better profile deliberately.
// =============================================================================
#pragma once

#include <sattari_audio_core/SattariKeyDetector.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariLogSpec.h>
#include <sattari_audio_core/SattariKeyNetSpec.h>

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace sattari::dsp
{

//  The published profile sets, selectable at RUNTIME. The numbering matches
//  SATTARI_KEY_PROFILE so the two cannot drift apart.
enum class KeyProfile
{
    Krumhansl = 0,
    Temperley = 1,
    Aarden    = 2,     // the historical global default
    Shaath    = 3,     // best measured on both corpora, 2026-08-11
    Bellman   = 4,
    Empirical = 6,
};

//  Which path produced an answer. Callers that gate on trust need to know
//  whether they are looking at a model verdict or a profile correlation,
//  because the two confidences are NOT the same quantity: the neural
//  confidence is a softmax probability in [0,1], the chroma confidence is a
//  Pearson margin between the winner and runner-up. Comparing them numerically
//  is meaningless and this enum exists to make that mistake hard.
enum class KeyAnalysisSource { None, Chroma, Neural };

struct KeyAnalysisConfig
{
    KeyProfile profile = KeyProfile::Aarden;   // matches the historical default
    bool       useNeural = false;              // opt IN; default preserves behaviour
};

struct KeyAnalysisResult
{
    int    root  = 0;          // 0..11 (C..B)
    bool   minor = false;
    double confidence = 0.0;   // see KeyAnalysisSource — NOT comparable across sources
    bool   valid = false;
    KeyAnalysisSource source = KeyAnalysisSource::None;
};

namespace detail
{
    inline const double* profileMajor (KeyProfile p) noexcept
    {
        switch (p)
        {
            case KeyProfile::Krumhansl: return kKrumhanslMajor;
            case KeyProfile::Temperley: return kTemperleyMajor;
            case KeyProfile::Shaath:    return kShaathMajor;
            case KeyProfile::Bellman:   return kBellmanMajor;
            case KeyProfile::Empirical: return kEmpiricalMajor;
            case KeyProfile::Aarden:
            default:                    return kAardenMajor;
        }
    }

    inline const double* profileMinor (KeyProfile p) noexcept
    {
        switch (p)
        {
            case KeyProfile::Krumhansl: return kKrumhanslMinor;
            case KeyProfile::Temperley: return kTemperleyMinor;
            case KeyProfile::Shaath:    return kShaathMinor;
            case KeyProfile::Bellman:   return kBellmanMinor;
            case KeyProfile::Empirical: return kEmpiricalMinor;
            case KeyProfile::Aarden:
            default:                    return kAardenMinor;
        }
    }
} // namespace detail

class KeyAnalysisEngine
{
public:
    //  --- neural model, optional in every sense -----------------------------
    //  A caller that never loads a model, or whose build has no ONNX, gets the
    //  chroma path and a valid answer. The Compass rule applies to the engine
    //  itself: the model may ENHANCE, it must not be load-bearing.
    bool loadNeuralModel (const std::string& onnxPath)
    {
        return net.loadModel (onnxPath);
    }

    bool neuralAvailable() const noexcept { return net.available(); }

    //  --- chroma path -------------------------------------------------------
    //  For realtime callers that already maintain their own rolling chroma
    //  (Compass and Auto Pitch both do). No allocation, no model, no I/O.
    KeyAnalysisResult analyseChroma (const double chroma[12],
                                     const KeyAnalysisConfig& cfg = {}) const noexcept
    {
        const auto k = detail::detectKeyWithProfiles (chroma,
                                                      detail::profileMajor (cfg.profile),
                                                      detail::profileMinor (cfg.profile));
        KeyAnalysisResult r;
        r.root = k.root; r.minor = k.minor;
        r.confidence = k.confidence; r.valid = k.valid;
        r.source = k.valid ? KeyAnalysisSource::Chroma : KeyAnalysisSource::None;
        return r;
    }

    //  --- whole-buffer path -------------------------------------------------
    //  Offline analysis of a complete take. Prefers the model when it is loaded
    //  AND the caller asked for it, and falls back to chroma otherwise — the
    //  fallback is not an error path, it is the documented behaviour when no
    //  model is installed.
    //
    //  NOT REALTIME SAFE: allocates, resamples, and runs inference. Call it
    //  from a worker thread, which is what both consumers already do.
    KeyAnalysisResult analyse (const float* mono, int numSamples, double sampleRate,
                               const KeyAnalysisConfig& cfg = {})
    {
        if (cfg.useNeural && net.available())
        {
            std::vector<float> spec;
            if (spec_.compute (mono, numSamples, sampleRate, spec))
            {
                const auto n = net.detectTTA (spec.data());
                if (n.valid)
                {
                    KeyAnalysisResult r;
                    r.root = n.root; r.minor = n.minor;
                    r.confidence = (double) n.confidence;
                    r.valid = true;
                    r.source = KeyAnalysisSource::Neural;
                    return r;
                }
            }
            //  Inference declined or the front-end failed: fall through to
            //  chroma rather than returning nothing. A conductor with no key is
            //  worse than a conductor with a profile-derived one.
        }
        return analyseChroma (rollingChroma (mono, numSamples, sampleRate).data(), cfg);
    }

    //  Exposed so evaluation tools can score the chroma front-end and the
    //  detector separately, which is how the profile comparison was measured.
    std::array<double, 12> rollingChroma (const float* mono, int numSamples,
                                          double sampleRate) const
    {
        HpcpChromagram hpcp;
        constexpr int kFft = 4096;
        const int half = kFft / 2 + 1;
        hpcp.reserveBins (half);
        hpcp.reset();

        std::vector<float> frame ((size_t) kFft * 2, 0.0f);
        juce::dsp::FFT fft (12);
        std::vector<float> window ((size_t) kFft);
        for (int i = 0; i < kFft; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265358979f
                                                         * (float) i / (float) (kFft - 1));

        for (int pos = 0; pos + kFft <= numSamples; pos += kFft / 2)
        {
            for (int i = 0; i < kFft; ++i)
                frame[(size_t) i] = mono[pos + i] * window[(size_t) i];
            std::fill (frame.begin() + kFft, frame.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (frame.data());
            hpcp.accumulate (frame.data(), half, sampleRate / (double) kFft);
        }

        std::array<double, 12> out {};
        hpcp.finish (out.data());
        return out;
    }

private:
    KeyNetSpec net;
    LogSpec    spec_;
};

} // namespace sattari::dsp
