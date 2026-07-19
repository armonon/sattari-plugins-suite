#pragma once

// =============================================================================
//  NeuralKeyEngine — trained neural key detection for StemDeck.
// -----------------------------------------------------------------------------
//  StemDeck's built-in key detection (TrackAnalyzer) is a chroma + Krumhansl
//  profile correlation. The suite already ships a trained model (SattariKeyNet,
//  a 24-class classifier over a 90-dim HPCP chroma feature) that measured 58%
//  exact vs 45% for the profile method, so when that model is installed we use
//  it and fall back to the DSP result when it isn't.
//
//  CRITICAL — feature parity: the model only works if we hand it the exact
//  feature it was TRAINED on. That recipe is core/tools/key_dataset_eval.cpp ::
//  fileChromaHpcpCov, and it is reproduced here verbatim:
//    4096-pt FFT, Hann window, hop = 2048, mono sum,
//    per-frame HpcpChromagram -> 12 bins, each frame L1-normalised,
//    then mean(12) + covariance(12x12), packed as
//    feat[0..11] = mean, feat[12..89] = cov upper-triangle incl. diag (i <= j).
//  Any deviation puts the model out of distribution and it will quietly degrade,
//  so keep this in sync with fileChromaHpcpCov if that ever changes.
//
//  This deliberately lives OUTSIDE TrackAnalysis.cpp: that file is kept free of
//  ONNX Runtime so the light DSP test targets (beat/key/automix/resampler) can
//  compile it standalone. Callers apply this on top of TrackAnalyzer's result.
// =============================================================================

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariKeyNet.h>

class NeuralKeyEngine
{
public:
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize  = 1 << kFftOrder;   // 4096 — must match training

    // The model Auto-Pitch also uses:
    //   <userApplicationDataDirectory>/Sattari Audio/models/keynet.onnx
    // (on macOS userApplicationDataDirectory is ~/Library, so ~/Library/Sattari Audio/...)
    static juce::File defaultModelFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Sattari Audio").getChildFile ("models").getChildFile ("keynet.onnx");
    }

    // Loads the model if present. Safe to call when ONNX isn't compiled in — the
    // engine simply reports unavailable and callers keep the DSP key.
    bool loadDefaultModel()
    {
        const auto f = defaultModelFile();
        if (! f.existsAsFile())
            return false;
        return keyNet.loadModel (f.getFullPathName().toStdString());
    }

    bool isAvailable() const noexcept { return keyNet.available(); }

    struct Result
    {
        bool  valid      = false;
        int   root       = 0;       // pitch class, 0 = C
        bool  minor      = false;
        float confidence = 0.0f;
    };

    // BLOCKING — background thread only (does a full-file FFT sweep + inference).
    Result detect (const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        Result r;
        if (! isAvailable() || audio.getNumSamples() < kFftSize || sampleRate <= 0.0)
            return r;

        // Mono sum (matches fileChromaHpcpCov's caller).
        const int n  = audio.getNumSamples();
        const int ch = juce::jmax (1, audio.getNumChannels());
        std::vector<float> mono ((size_t) n, 0.0f);
        for (int c = 0; c < ch; ++c)
        {
            const auto* d = audio.getReadPointer (c);
            for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i];
        }
        for (auto& v : mono) v /= (float) ch;

        double mean[12]; double cov[12][12];
        chromaHpcpCov (mono, sampleRate, mean, cov);

        // Pack: mean(12) then covariance upper-triangle incl. diagonal (78) = 90.
        float feat[90]; int fi = 0;
        for (int i = 0; i < 12; ++i) feat[fi++] = (float) mean[i];
        for (int i = 0; i < 12; ++i)
            for (int j = i; j < 12; ++j)
                feat[fi++] = (float) cov[i][j];
        jassert (fi == 90);

        const auto kn = keyNet.detectTTA (feat);   // test-time augmentation (+2.7 exact)
        if (! kn.valid)
            return r;

        r.valid = true; r.root = kn.root; r.minor = kn.minor; r.confidence = kn.confidence;
        return r;
    }

private:
    // Verbatim port of core/tools/key_dataset_eval.cpp :: fileChromaHpcpCov.
    void chromaHpcpCov (const std::vector<float>& mono, double sampleRate,
                        double mean[12], double cov[12][12])
    {
        const double binHz = sampleRate / (double) kFftSize;
        const int hop = kFftSize / 2;
        const juce::int64 n = (juce::int64) mono.size();

        for (int i = 0; i < 12; ++i) { mean[i] = 0.0; for (int j = 0; j < 12; ++j) cov[i][j] = 0.0; }

        juce::dsp::FFT fft (kFftOrder);
        std::array<float, kFftSize> hann {};
        for (int i = 0; i < kFftSize; ++i)
            hann[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                       * (float) i / (float) (kFftSize - 1));
        std::array<float, kFftSize * 2> scratch {};

        int frames = 0;
        for (juce::int64 start = 0; start == 0 || start + kFftSize <= n; start += hop)
        {
            for (int i = 0; i < kFftSize; ++i)
            {
                const juce::int64 idx = start + i;
                const float s = (idx < n) ? mono[(size_t) idx] : 0.0f;
                scratch[(size_t) i] = s * hann[(size_t) i];
            }
            std::fill (scratch.begin() + kFftSize, scratch.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (scratch.data());

            sattari::dsp::HpcpChromagram hpcp; hpcp.reset();
            hpcp.accumulate (scratch.data(), kFftSize / 2 + 1, binHz);
            double c12[12]; hpcp.finish (c12);

            double s = 0.0; for (int i = 0; i < 12; ++i) s += c12[i];
            if (s > 1.0e-9)
            {
                for (int i = 0; i < 12; ++i) c12[i] /= s;      // per-frame L1 normalise
                ++frames;
                for (int i = 0; i < 12; ++i)
                {
                    mean[i] += c12[i];
                    for (int j = 0; j < 12; ++j) cov[i][j] += c12[i] * c12[j];
                }
            }
            if (start + kFftSize > n) break;
        }

        if (frames > 0)
        {
            for (int i = 0; i < 12; ++i) mean[i] /= frames;
            for (int i = 0; i < 12; ++i)
                for (int j = 0; j < 12; ++j)
                    cov[i][j] = cov[i][j] / frames - mean[i] * mean[j];
        }
    }

    sattari::dsp::KeyNet keyNet;
};
