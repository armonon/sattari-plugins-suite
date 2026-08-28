#pragma once

// =============================================================================
//  Sattari Song Key — full-mix key estimator
// -----------------------------------------------------------------------------
//  This is the offline/live-listen analysis path for Sattari Key. It is kept
//  separate from KeyAnalysisEngine's historical Aarden path so Compass and the
//  frozen Auto Pitch candidate do not change when the song analyser improves.
//
//  The front end is intentionally fixed and deterministic:
//    * mono box-resampling to 16 kHz;
//    * 4096-sample, non-overlapping, area-normalised Hann frames;
//    * the 60 strongest interpolated peaks from 25 Hz to 3.5 kHz;
//    * local spectral-envelope whitening;
//    * four-harmonic, cosine-weighted pitch-class accumulation;
//    * whole-song averaging, 0.2 peak-relative gating, and balanced
//      major/minor profile correlation.
//
//  It is a self-contained implementation. No model, network access, service,
//  or third-party runtime is required by the product.
// =============================================================================

#include <sattari_audio_core/SattariKeyAnalysis.h>

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sattari::dsp
{

class SongKeyEstimator
{
public:
    static constexpr double kAnalysisSampleRate = 16000.0;
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kSpectrumBins = kFftSize / 2 + 1;
    static constexpr int kSectionFrames = 59; // approximately 15 seconds

    SongKeyEstimator()
        : fft_ (kFftOrder)
    {
        double sum = 0.0;
        for (int i = 0; i < kFftSize; ++i)
        {
            const double phase = 2.0 * juce::MathConstants<double>::pi
                               * (double) i / (double) (kFftSize - 1);
            window_[(size_t) i] = (float) (0.5 - 0.5 * std::cos (phase));
            sum += window_[(size_t) i];
        }
        const float areaScale = sum > 0.0 ? (float) (2.0 / sum) : 1.0f;
        for (auto& value : window_)
            value *= areaScale;
        peaks_.reserve (kSpectrumBins);
    }

    // `totalInputSamples` enables byte-for-byte deterministic end handling for
    // file scans. Leave it at zero for an open-ended live stream.
    void prepare (double inputSampleRate, std::int64_t totalInputSamples = 0)
    {
        inputSampleRate_ = inputSampleRate > 0.0 ? inputSampleRate : 44100.0;
        ratio_ = inputSampleRate_ / kAnalysisSampleRate;
        if (ratio_ <= 0.0) ratio_ = 1.0;
        targetOutputSamples_ = totalInputSamples > 0
            ? (std::int64_t) std::llround ((double) totalInputSamples / ratio_)
            : 0;
        reset();
    }

    void reset()
    {
        inputSamples_ = 0;
        outputSamples_ = 0;
        nextBoundary_ = roundedBoundary (1);
        bucketSum_ = 0.0;
        bucketCount_ = 0;
        lastInput_ = 0.0f;
        frameFill_ = 0;
        frames_ = 0;
        sectionFrameCount_ = 0;
        globalPcp_.fill (0.0);
        sectionPcp_.fill (0.0);
        frame_.fill (0.0f);
        spectrum_.fill (0.0f);
        sections_.clear();
        finalised_ = false;
    }

    void addSamples (const float* mono, int numSamples)
    {
        if (mono == nullptr || numSamples <= 0 || finalised_) return;

        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = std::isfinite (mono[i]) ? mono[i] : 0.0f;
            lastInput_ = sample;
            bucketSum_ += sample;
            ++bucketCount_;
            ++inputSamples_;

            while (inputSamples_ >= nextBoundary_
                   && (targetOutputSamples_ == 0
                       || outputSamples_ < targetOutputSamples_))
            {
                emitResampled (bucketCount_ > 0
                    ? (float) (bucketSum_ / (double) bucketCount_)
                    : lastInput_);
                bucketSum_ = 0.0;
                bucketCount_ = 0;
                nextBoundary_ = roundedBoundary (outputSamples_ + 1);
            }
        }
    }

    void finalise()
    {
        if (finalised_) return;

        if (targetOutputSamples_ > 0)
        {
            while (outputSamples_ < targetOutputSamples_)
            {
                emitResampled (bucketCount_ > 0
                    ? (float) (bucketSum_ / (double) bucketCount_)
                    : lastInput_);
                bucketSum_ = 0.0;
                bucketCount_ = 0;
                nextBoundary_ = roundedBoundary (outputSamples_ + 1);
            }
        }

        if (frameFill_ > 0)
        {
            std::fill (frame_.begin() + frameFill_, frame_.end(), 0.0f);
            analyseFrame();
            frameFill_ = 0;
        }
        finishSection();
        finalised_ = true;
    }

    int framesAnalysed() const noexcept { return frames_; }
    const std::vector<KeyAnalysisResult>& sections() const noexcept { return sections_; }

    std::array<double, 12> chroma() const noexcept
    {
        std::array<double, 12> pcp = globalPcp_;
        gateAndNormalise (pcp, frames_);
        return pcp;
    }

    KeyAnalysisResult result() const noexcept
    {
        return decide (chroma()).primary;
    }

    KeyCandidate alternative() const noexcept
    {
        return decide (chroma()).alternative;
    }

private:
    struct Peak
    {
        double magnitude = 0.0;
        double frequency = 0.0;
    };

    struct Decision
    {
        KeyAnalysisResult primary;
        KeyCandidate alternative;
    };

    std::int64_t roundedBoundary (std::int64_t outputCount) const noexcept
    {
        return std::max<std::int64_t> (1,
            (std::int64_t) std::llround ((double) outputCount * ratio_));
    }

    void emitResampled (float sample)
    {
        if (targetOutputSamples_ > 0 && outputSamples_ >= targetOutputSamples_)
            return;
        frame_[(size_t) frameFill_++] = sample;
        ++outputSamples_;
        if (frameFill_ == kFftSize)
        {
            analyseFrame();
            frameFill_ = 0;
        }
    }

    static double safeDb (double power) noexcept
    {
        return power > 1.0e-30 ? 10.0 * std::log10 (power) : -300.0;
    }

    static double interpolateEnvelope (const std::array<double, 43>& envelopeDb,
                                       double frequency) noexcept
    {
        const double position = std::clamp (frequency / 100.0, 0.0, 42.0);
        const int lo = (int) std::floor (position);
        const int hi = std::min (42, lo + 1);
        const double fraction = position - (double) lo;
        return envelopeDb[(size_t) lo]
             + fraction * (envelopeDb[(size_t) hi] - envelopeDb[(size_t) lo]);
    }

    void analyseFrame()
    {
        for (int i = 0; i < kFftSize; ++i)
            spectrum_[(size_t) i] = frame_[(size_t) i] * window_[(size_t) i];
        std::fill (spectrum_.begin() + kFftSize, spectrum_.end(), 0.0f);
        fft_.performFrequencyOnlyForwardTransform (spectrum_.data());

        constexpr double binHz = kAnalysisSampleRate / (double) kFftSize;
        const int firstBin = (int) std::ceil (25.0 / binHz);
        const int lastBin = std::min (kSpectrumBins - 2,
                                      (int) std::floor (3500.0 / binHz));
        peaks_.clear();
        // A descending spectrum at the lower analysis boundary owns a boundary
        // peak exactly at that bin. Interpolating it against an out-of-band
        // neighbour would pull sub-bass energy below 25 Hz and can flip a close
        // dominant/tonic decision.
        if (spectrum_[(size_t) firstBin] > spectrum_[(size_t) (firstBin + 1)]
            && spectrum_[(size_t) firstBin] > 0.0001f)
            peaks_.push_back ({ spectrum_[(size_t) firstBin],
                                (double) firstBin * binHz });

        for (int bin = firstBin + 1; bin <= lastBin; ++bin)
        {
            const double left = spectrum_[(size_t) (bin - 1)];
            const double middle = spectrum_[(size_t) bin];
            const double right = spectrum_[(size_t) (bin + 1)];
            if (! (middle > left && middle >= right && middle > 0.0001))
                continue;

            const double denominator = left - 2.0 * middle + right;
            const double delta = std::fabs (denominator) > 1.0e-30
                ? 0.5 * (left - right) / denominator : 0.0;
            const double magnitude = middle - 0.25 * (left - right) * delta;
            peaks_.push_back ({ magnitude, ((double) bin + delta) * binHz });
        }
        std::sort (peaks_.begin(), peaks_.end(), [] (const Peak& a, const Peak& b)
        {
            if (a.magnitude > b.magnitude) return true;
            if (b.magnitude > a.magnitude) return false;
            return a.frequency < b.frequency;
        });
        if (peaks_.size() > 60) peaks_.resize (60);

        std::array<double, 43> envelopeDb {};
        for (int point = 0; point < 43; ++point)
        {
            const double frequency = (double) point * 100.0;
            const double beginHz = frequency - std::max (50.0, frequency * 0.34);
            const double endHz = frequency + std::max (50.0, frequency * 0.58);
            int begin = (int) std::floor (beginHz / 8000.0
                                          * (double) (kSpectrumBins - 1) + 0.5);
            int end = (int) std::floor (endHz / 8000.0
                                        * (double) (kSpectrumBins - 1) + 0.5);
            begin = std::clamp (begin, 0, kSpectrumBins - 1);
            end = std::clamp (end, begin + 1, kSpectrumBins);
            const double centre = 0.5 * ((double) begin + (double) end);
            const double halfLength = (double) end - centre;
            double numerator = 0.0;
            double denominator = 0.0;
            for (int bin = begin; bin < end; ++bin)
            {
                double shape = 1.0 - std::fabs ((double) bin - centre) / halfLength;
                shape *= shape;
                shape *= shape;
                const double energy = (double) spectrum_[(size_t) bin]
                                    * (double) spectrum_[(size_t) bin];
                const double weight = shape * energy;
                numerator += energy * weight;
                denominator += weight;
            }
            const double envelopePower = denominator > 0.0
                ? numerator / denominator : 0.0;
            envelopeDb[(size_t) point] = safeDb (envelopePower);
        }
        envelopeDb.back() = envelopeDb[envelopeDb.size() - 2];

        std::array<double, 12> framePcp {};
        for (const auto& peak : peaks_)
        {
            const double peakDb = 20.0 * std::log10 (std::max (peak.magnitude, 1.0e-30));
            const double localEnvelope = interpolateEnvelope (envelopeDb, peak.frequency);
            double whitenedDb = -200.0;
            if (peakDb > localEnvelope)
                whitenedDb = 0.0;
            else if (peakDb > localEnvelope - 30.0)
                whitenedDb = peakDb - localEnvelope;
            whitenedDb -= 20.0 * peak.frequency / 4000.0;
            const double whitenedMagnitude = std::pow (10.0, whitenedDb / 20.0);

            // Harmonics 1, 2 and 4 reinforce the same pitch class. Harmonics 3
            // and 5 contribute their own sub-semitone hypotheses.
            addPitchContribution (peak.frequency, whitenedMagnitude, 0.0, 3.0, framePcp);
            addPitchContribution (peak.frequency, whitenedMagnitude,
                                  12.0 * std::log2 (3.0) - 12.0, 1.0, framePcp);
            const double fifthSemitone = 12.0 * std::log2 (5.0) - 24.0;
            const double fifthStrength = 1.0
                / std::max (1.0, 0.5 * std::log2 (5.0));
            addPitchContribution (peak.frequency, whitenedMagnitude,
                                  fifthSemitone, fifthStrength, framePcp);
        }

        for (int pitch = 0; pitch < 12; ++pitch)
        {
            globalPcp_[(size_t) pitch] += framePcp[(size_t) pitch];
            sectionPcp_[(size_t) pitch] += framePcp[(size_t) pitch];
        }
        ++frames_;
        if (++sectionFrameCount_ >= kSectionFrames)
            finishSection();
    }

    static void addPitchContribution (double peakFrequency,
                                      double magnitude,
                                      double harmonicSemitones,
                                      double harmonicStrength,
                                      std::array<double, 12>& pcp) noexcept
    {
        const double frequency = peakFrequency
                               * std::pow (2.0, -harmonicSemitones / 12.0);
        if (! (frequency > 0.0)) return;
        const double binPosition = 12.0 * std::log2 (frequency / 440.0);
        const int left = (int) std::ceil (binPosition - 0.5);
        const int right = (int) std::floor (binPosition + 0.5);
        const double energy = magnitude * magnitude
                            * harmonicStrength * harmonicStrength;
        for (int bin = left; bin <= right; ++bin)
        {
            const double distance = std::fabs (binPosition - (double) bin);
            const double weight = std::cos (juce::MathConstants<double>::pi * distance);
            const int wrapped = ((bin % 12) + 12) % 12;
            pcp[(size_t) wrapped] += weight * energy;
        }
    }

    static void gateAndNormalise (std::array<double, 12>& pcp, int frameCount) noexcept
    {
        if (frameCount <= 0)
        {
            pcp.fill (0.0);
            return;
        }
        for (auto& value : pcp) value /= (double) frameCount;
        const double peak = *std::max_element (pcp.begin(), pcp.end());
        if (peak <= 1.0e-30)
        {
            pcp.fill (0.0);
            return;
        }
        for (auto& value : pcp)
        {
            value /= peak;
            if (value < 0.2) value = 0.0;
        }
    }

    static double correlation (const std::array<double, 12>& pcp,
                               const double profile[12], int shift) noexcept
    {
        double pcpMean = 0.0, profileMean = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            pcpMean += pcp[(size_t) i];
            profileMean += profile[i];
        }
        pcpMean /= 12.0;
        profileMean /= 12.0;
        double numerator = 0.0, pcpEnergy = 0.0, profileEnergy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = pcp[(size_t) i] - pcpMean;
            const int profileIndex = ((i - shift) % 12 + 12) % 12;
            const double b = profile[profileIndex] - profileMean;
            numerator += a * b;
            pcpEnergy += a * a;
            profileEnergy += b * b;
        }
        const double denominator = std::sqrt (pcpEnergy * profileEnergy);
        return denominator > 1.0e-30 ? numerator / denominator : -1.0;
    }

    static Decision decide (const std::array<double, 12>& pcp) noexcept
    {
        static constexpr double major[12] = {
            1.00, 0.00, 0.42, 0.00, 0.53, 0.37,
            0.00, 0.77, 0.00, 0.38, 0.21, 0.30
        };
        static constexpr double minor[12] = {
            1.00, 0.00, 0.36, 0.39, 0.00, 0.38,
            0.00, 0.74, 0.27, 0.00, 0.42, 0.23
        };

        Decision out;
        double majorBest = -2.0, minorBest = -2.0;
        int majorShift = 0, minorShift = 0;
        std::array<double, 24> scores {};
        for (int shift = 0; shift < 12; ++shift)
        {
            scores[(size_t) shift] = correlation (pcp, major, shift);
            scores[(size_t) (12 + shift)] = correlation (pcp, minor, shift);
            if (scores[(size_t) shift] > majorBest)
            {
                majorBest = scores[(size_t) shift];
                majorShift = shift;
            }
            if (scores[(size_t) (12 + shift)] > minorBest)
            {
                minorBest = scores[(size_t) (12 + shift)];
                minorShift = shift;
            }
        }

        const bool isMinor = minorBest >= majorBest;
        const int winningShift = isMinor ? minorShift : majorShift;
        const int winningIndex = (isMinor ? 12 : 0) + winningShift;
        const double winningScore = scores[(size_t) winningIndex];
        if (! std::isfinite (winningScore) || winningScore <= -1.0)
            return out;

        out.primary.root = (winningShift + 9) % 12; // A-indexed PCP -> C-indexed suite
        out.primary.minor = isMinor;
        out.primary.valid = true;
        out.primary.source = KeyAnalysisSource::Chroma;

        double alternativeScore = -2.0;
        int alternativeIndex = -1;
        for (int i = 0; i < 24; ++i)
        {
            if (i == winningIndex) continue;
            if (scores[(size_t) i] > alternativeScore)
            {
                alternativeScore = scores[(size_t) i];
                alternativeIndex = i;
            }
        }
        if (alternativeIndex >= 0)
        {
            const int shift = alternativeIndex % 12;
            out.alternative.root = (shift + 9) % 12;
            out.alternative.minor = alternativeIndex >= 12;
            out.alternative.score = alternativeScore;
            out.alternative.valid = true;
        }
        out.primary.confidence = std::clamp (
            winningScore - (alternativeIndex >= 0 ? alternativeScore : 0.0),
            0.0, 1.0);
        return out;
    }

    void finishSection()
    {
        if (sectionFrameCount_ <= 0) return;
        auto pcp = sectionPcp_;
        gateAndNormalise (pcp, sectionFrameCount_);
        const auto section = decide (pcp).primary;
        if (section.valid) sections_.push_back (section);
        sectionPcp_.fill (0.0);
        sectionFrameCount_ = 0;
    }

    juce::dsp::FFT fft_;
    std::array<float, kFftSize> window_ {};
    std::array<float, kFftSize> frame_ {};
    std::array<float, kFftSize * 2> spectrum_ {};
    std::vector<Peak> peaks_;
    std::array<double, 12> globalPcp_ {};
    std::array<double, 12> sectionPcp_ {};
    std::vector<KeyAnalysisResult> sections_;

    double inputSampleRate_ = 44100.0;
    double ratio_ = inputSampleRate_ / kAnalysisSampleRate;
    std::int64_t targetOutputSamples_ = 0;
    std::int64_t inputSamples_ = 0;
    std::int64_t outputSamples_ = 0;
    std::int64_t nextBoundary_ = 1;
    double bucketSum_ = 0.0;
    int bucketCount_ = 0;
    float lastInput_ = 0.0f;
    int frameFill_ = 0;
    int frames_ = 0;
    int sectionFrameCount_ = 0;
    bool finalised_ = false;
};

} // namespace sattari::dsp
