#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariKWeighting.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  Peak — true-peak lookahead limiter with optional target-loudness drive.
// -----------------------------------------------------------------------------
//  For every incoming sample the detector computes the gain that would land it
//  exactly on the ceiling, and stores it alongside the (pushed) dry sample in a
//  lookahead ring. The envelope then attacks instantly to the MINIMUM required
//  gain anywhere in the window, so the reduction is fully in place before the loud
//  sample reaches the output, and releases with a program-dependent one-pole.
//  That gives the guarantee: a delayed output sample is scaled by <= its own
//  required gain. A soft tanh knee under the ceiling catches residual inter-sample
//  overshoot so |out| never crosses it.
//
//  Target Loudness slowly adapts an extra push from the OUTPUT's short-term LUFS.
//  It is bounded and the ceiling always holds regardless of how much push is asked
//  for, so it cannot run away.
//
//  LATENCY: this is the first module with a non-zero latency. It reports the
//  current lookahead through latencySamples() and NEVER calls setLatencySamples()
//  itself — the shell (standalone or rack) publishes it off the audio thread. The
//  standalone plugin used to call setLatencySamples() from inside processBlock,
//  which is not realtime-safe; routing it through the interface fixes that.
// =============================================================================

namespace sattari::dsp
{

class PeakModule final : public SattariModule
{
public:
    const char* moduleId()    const noexcept override { return "peak"; }
    const char* displayName() const noexcept override { return "Peak"; }

    struct Readout
    {
        float gainReductionDb = 0.0f;     // current attenuation (>= 0)
        float truePeakDb      = -100.0f;  // output true-peak
        float shortLufs       = -100.0f;  // short-term loudness (out)
        float pushDb          = 0.0f;     // effective push applied (incl. auto)
    };

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "push", 1 }, "Push",
            juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "ceiling", 1 }, "Ceiling",
            juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f), -1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dBTP")));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "release", 1 }, "Release",
            juce::NormalisableRange<float> (1.0f, 1000.0f, 0.1f, 0.4f), 120.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "lookahead", 1 }, "Lookahead",
            juce::NormalisableRange<float> (0.5f, 10.0f, 0.01f), 3.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { prefix + "truePeak", 1 }, "True-Peak Detect", true));
        out.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { prefix + "autoLoud", 1 }, "Target Loudness", false));
        out.push_back (std::make_unique<FP> (juce::ParameterID { prefix + "targetLufs", 1 }, "Target LUFS",
            juce::NormalisableRange<float> (-24.0f, -6.0f, 0.1f), -14.0f,
            juce::AudioParameterFloatAttributes().withLabel ("LUFS")));
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        pPush   = apvts.getRawParameterValue (prefix + "push");
        pCeil   = apvts.getRawParameterValue (prefix + "ceiling");
        pRel    = apvts.getRawParameterValue (prefix + "release");
        pLook   = apvts.getRawParameterValue (prefix + "lookahead");
        pTP     = apvts.getRawParameterValue (prefix + "truePeak");
        pAuto   = apvts.getRawParameterValue (prefix + "autoLoud");
        pTarget = apvts.getRawParameterValue (prefix + "targetLufs");
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Lookahead delay line sized for the 10 ms maximum (allocation-free after this).
        maxLookaheadSamples = juce::jmax (1, (int) std::ceil (0.010 * sr_));
        ringLen = maxLookaheadSamples + 1;
        lookaheadRing.setSize (2, ringLen);

        for (auto& b : shelf) designKWeightingShelf (b, sr_);
        for (auto& b : hp)    designKWeightingHighPass (b, sr_);
        shortLen = juce::jmax (1, (int) std::round (sr_ * 3.0));
        zCap = shortLen;
        zRing.assign ((size_t) zCap, 0.0f);

        reset();

        // The lookahead delay is live from the FIRST sample, so the latency has to be
        // known here — before the host asks. Deriving it from the parameter (rather
        // than waiting for process() to record it) is what keeps the reported latency
        // honest: a host that prepared and immediately compared in-to-out used to see
        // a lookahead-sized misalignment, because it was told the latency was 0.
        currentLook.store (lookaheadSamples(), std::memory_order_relaxed);
    }

    void reset() override
    {
        lookaheadRing.clear();
        // Empty slots mean "no reduction required" -> unity gain (1.0), NOT 0.0.
        // Priming with 0 would make the window-min force a full duck on the first block.
        peakRing.assign ((size_t) juce::jmax (1, ringLen), 1.0f);
        ringPos = 0;
        envGain = 1.0f;
        tpHist = { 0.0f, 0.0f };
        outTpHist = { 0.0f, 0.0f };
        tpMax = 0.0f;
        for (auto& b : shelf) b.reset();
        for (auto& b : hp)    b.reset();
        std::fill (zRing.begin(), zRing.end(), 0.0f);
        zWrite = 0; shortSum = 0.0; autoPushDb = 0.0f;
        grDb.store (0.0f, std::memory_order_relaxed);
        tpDb.store (-100.0f, std::memory_order_relaxed);
        shortLufs.store (-100.0f, std::memory_order_relaxed);
        pushDbOut.store (0.0f, std::memory_order_relaxed);
        // currentLook is deliberately NOT cleared: a flush is not a latency change, and
        // zeroing it here would make a mid-stream reset() announce a bogus 0 to the host.
    }

    // Current lookahead in samples. The shell publishes this to the host; the audio
    // thread only ever stores it.
    int latencySamples() const noexcept override { return currentLook.load (std::memory_order_relaxed); }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0 || ringLen <= 0 || zCap <= 0) return;

        const float pushDb     = val (pPush, 0.0f);
        const float ceilingDb  = val (pCeil, -1.0f);
        const float releaseMs  = juce::jmax (1.0f, val (pRel, 120.0f));
        const float lookMs     = val (pLook, 3.0f);
        const bool  truePeak   = val (pTP, 1.0f) > 0.5f;
        const bool  autoLoud   = val (pAuto, 0.0f) > 0.5f;
        const float targetLufs = val (pTarget, -14.0f);

        const float ceilingLin = dbToGain (ceilingDb);

        // Lookahead in samples. Recorded for the shell to publish — never announced
        // to the host from here, because that is not realtime-safe.
        const int look = lookaheadSamples (lookMs);
        currentLook.store (look, std::memory_order_relaxed);

        // Program-dependent release: one-pole recovery toward unity gain.
        relCoeff = std::exp (-1.0f / (float) (sr_ * (releaseMs * 0.001f)));

        // Auto target-loudness: nudge an extra push toward the target based on the
        // last block's short-term loudness. Slow + bounded so it can't runaway; the
        // ceiling always holds regardless of how much push is requested.
        if (autoLoud)
        {
            const float lastShort = shortLufs.load (std::memory_order_relaxed);
            if (lastShort > -60.0f)
            {
                const float errDb = targetLufs - lastShort;              // positive => need louder
                autoPushDb += juce::jlimit (-0.5f, 0.5f, errDb * 0.05f); // small step per block
                autoPushDb = juce::jlimit (0.0f, 24.0f, autoPushDb);
            }
        }
        else
        {
            // Relax the auto drive back toward zero so toggling off is graceful.
            autoPushDb *= 0.95f;
            if (autoPushDb < 0.01f) autoPushDb = 0.0f;
        }

        const float effectivePushDb = juce::jlimit (0.0f, 36.0f, pushDb + (autoLoud ? autoPushDb : 0.0f));
        const float pushGain = dbToGain (effectivePushDb);
        pushDbOut.store (effectivePushDb, std::memory_order_relaxed);

        float grForMeter = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // ---- Detector: push-scaled inter-sample (or sample) peak of this input.
            float detPeak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = buffer.getReadPointer (ch)[i] * pushGain;
                const float p = truePeak ? interSamplePeak (tpHist[(size_t) ch], x) : std::fabs (x);
                detPeak = std::max (detPeak, p);
                tpHist[(size_t) ch] = x;
            }

            // Required instantaneous gain so this (future) sample lands on the ceiling.
            const float reqGain = detPeak > ceilingLin ? ceilingLin / detPeak : 1.0f;

            // ---- Write pushed dry + its required gain into the delay slot.
            const int writePos = ringPos;
            for (int ch = 0; ch < numCh; ++ch)
                lookaheadRing.setSample (ch, writePos, buffer.getReadPointer (ch)[i] * pushGain);
            if (numCh == 1) lookaheadRing.setSample (1, writePos, lookaheadRing.getSample (0, writePos));
            peakRing[(size_t) writePos] = reqGain;

            // ---- Envelope: attack instantly to the MINIMUM required gain anywhere in
            // the lookahead window; otherwise release toward unity.
            float windowMin = 1.0f;
            for (int k = 0; k <= look; ++k)
            {
                const int idx = (writePos - k + ringLen) % ringLen;
                windowMin = std::min (windowMin, peakRing[(size_t) idx]);
            }
            if (windowMin < envGain) envGain = windowMin;                 // instant attack
            else envGain = windowMin + relCoeff * (envGain - windowMin);  // program-dependent release
            envGain = juce::jlimit (0.0f, 1.0f, envGain);

            // ---- Read the delayed dry sample and apply the envelope.
            const int readPos = (writePos - look + ringLen) % ringLen;
            float outSampleAbsTp = 0.0f;
            const int outCh = juce::jmin (2, buffer.getNumChannels());
            for (int ch = 0; ch < outCh; ++ch)
            {
                const int ringCh = juce::jmin (ch, 1);
                float y = lookaheadRing.getSample (ringCh, readPos) * envGain;

                // Soft tanh knee just under the ceiling catches residual inter-sample
                // overshoot so |out| never crosses the ceiling.
                const float lim = ceilingLin;
                if (std::fabs (y) > lim * 0.5f)
                {
                    const float sign = y < 0.0f ? -1.0f : 1.0f;
                    const float a = std::fabs (y);
                    const float over = (a - lim * 0.5f) / (lim * 0.5f);
                    const float shaped = lim * 0.5f + lim * 0.5f * std::tanh (over);
                    y = sign * shaped;
                }
                y = juce::jlimit (-lim, lim, y);

                buffer.getWritePointer (ch)[i] = y;

                const float otp = interSamplePeak (outTpHist[(size_t) ringCh], y);
                outSampleAbsTp = std::max (outSampleAbsTp, otp);
                outTpHist[(size_t) ringCh] = y;
            }

            tpMax = std::max (tpMax, outSampleAbsTp);
            grForMeter = std::max (grForMeter, 1.0f - envGain);

            // ---- Short-term loudness of the OUTPUT (K-weighted mono square, 3 s).
            double zsum = 0.0;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float y = buffer.getReadPointer (ch)[i];
                const double kw = hp[(size_t) ch].process (shelf[(size_t) ch].process ((double) y));
                zsum += kw * kw;
            }
            const float leaving = zRing[(size_t) zWrite];
            shortSum += zsum - leaving;
            zRing[(size_t) zWrite] = (float) zsum;
            if (++zWrite >= zCap) zWrite = 0;

            if (++ringPos >= ringLen) ringPos = 0;
        }

        const float minEnvGain = juce::jlimit (1.0e-4f, 1.0f, 1.0f - grForMeter);
        grDb.store (-juce::Decibels::gainToDecibels (minEnvGain, -60.0f), std::memory_order_relaxed);
        tpDb.store (tpMax > 0.0f ? juce::Decibels::gainToDecibels (tpMax) : -100.0f, std::memory_order_relaxed);
        tpMax *= 0.90f;   // slow decay so the reading falls back

        const double meanSq = shortSum / juce::jmax (1, shortLen);
        shortLufs.store (meanSq > 0.0 ? (float) (-0.691 + 10.0 * std::log10 (meanSq)) : -100.0f,
                         std::memory_order_relaxed);
    }

    Readout getReadout() const noexcept
    {
        Readout r;
        r.gainReductionDb = grDb.load (std::memory_order_relaxed);
        r.truePeakDb      = tpDb.load (std::memory_order_relaxed);
        r.shortLufs       = shortLufs.load (std::memory_order_relaxed);
        r.pushDb          = pushDbOut.load (std::memory_order_relaxed);
        return r;
    }

private:
    static float val (std::atomic<float>* p, float fallback) noexcept
    { return p != nullptr ? p->load() : fallback; }
    static float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

    // The one place the lookahead time becomes a sample count, so prepare() and
    // process() can never disagree about how much delay the output actually has.
    int lookaheadSamples (float lookMs) const noexcept
    { return juce::jlimit (1, maxLookaheadSamples, (int) std::lround (lookMs * 0.001 * sr_)); }
    int lookaheadSamples() const noexcept { return lookaheadSamples (val (pLook, 3.0f)); }

    std::atomic<float>* pPush = nullptr; std::atomic<float>* pCeil = nullptr;
    std::atomic<float>* pRel = nullptr;  std::atomic<float>* pLook = nullptr;
    std::atomic<float>* pTP = nullptr;   std::atomic<float>* pAuto = nullptr;
    std::atomic<float>* pTarget = nullptr;

    double sr_ = 44100.0;

    // --- Lookahead delay line + envelope --------------------------------------
    juce::AudioBuffer<float> lookaheadRing;   // stereo dry delay
    std::vector<float>       peakRing;         // detector peak per delayed sample slot
    int   ringLen = 1, ringPos = 0;
    int   maxLookaheadSamples = 0;
    std::atomic<int> currentLook { 0 };        // published by the shell, not by us

    float envGain = 1.0f;      // current smoothed limiter gain (<= 1)
    float relCoeff = 0.0f;     // release one-pole coefficient (per block)
    std::array<float, 2> tpHist { { 0.0f, 0.0f } };   // detector history for ISP

    // --- Auto target-LUFS drive -----------------------------------------------
    std::array<KBiquad, 2> shelf, hp;   // K-weighting per channel (output metering)
    std::vector<float> zRing;           // 3 s of K-weighted mono-square (short-term)
    int    zCap = 0, zWrite = 0;
    double shortSum = 0.0;
    int    shortLen = 0;
    float  autoPushDb = 0.0f;           // slowly-adapted extra push toward target

    // --- Output metering ------------------------------------------------------
    std::array<float, 2> outTpHist { { 0.0f, 0.0f } };
    float tpMax = 0.0f;

    std::atomic<float> grDb { 0.0f }, tpDb { -100.0f }, shortLufs { -100.0f }, pushDbOut { 0.0f };
};

} // namespace sattari::dsp
