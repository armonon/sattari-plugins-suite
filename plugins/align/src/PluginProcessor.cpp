#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

SattariAlignAudioProcessor::SattariAlignAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
          .withInput  ("Guide",     juce::AudioChannelSet::stereo(), false)
          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariAlignAudioProcessor::createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("autoAlign", 1), "Auto Align", true));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("lock",      1), "Lock Alignment", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("bypass",    1), "Bypass", false));
    // v1 = bulk-shift (single lag for the whole stream), v2 = DTW warping
    // (time-varying delay that follows local drift). v2 is alpha - the warp
    // path is recomputed every search cadence and smoothed into the delay so
    // sudden warp changes can pump on transients. Read the editor caption.
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("algorithm", 1), "Algorithm",
                                                                juce::StringArray { "Bulk Shift (v1)", "DTW Warp (v2 alpha)" }, 0));
    p.push_back (std::make_unique<P> (juce::ParameterID ("manualOffsetMs", 1), "Manual Offset",
                                       R (-150.0f, 150.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("searchRangeMs", 1), "Search Range",
                                       R (10.0f, 300.0f, 1.0f), 100.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("sensitivity", 1), "Sensitivity",
                                       R (0.0f, 1.0f, 0.01f), 0.55f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("smoothing", 1), "Smoothing",
                                       R (0.0f, 1.0f, 0.01f), 0.7f));

    return { p.begin(), p.end() };
}

void SattariAlignAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax (1, samplesPerBlock);

    // Onset-envelope ring (200 Hz internal rate) + decimation counter.
    mainEnv.assign ((size_t) envHistorySize, 0.0f);
    guideEnv.assign ((size_t) envHistorySize, 0.0f);
    envWritePos = 0;
    envSamplesPerHost = juce::jmax (1, (int) std::round (currentSampleRate / (double) envSampleRate));
    envAccumulator = 0;
    mainEnvAccum = guideEnvAccum = 0.0f;
    mainEnvCount = guideEnvCount = 0;
    mainPrevEnergy = guidePrevEnergy = 0.0f;

    // Light smoothing on the rectified envelope; sharp enough to keep transients.
    mainSmoother.prepare (currentSampleRate, 4.0f, 30.0f);
    guideSmoother.prepare (currentSampleRate, 4.0f, 30.0f);

    // Reserve enough delay headroom for the maximum search range (300 ms).
    maxDelaySamples = (int) std::ceil (0.300 * currentSampleRate) + preparedBlockSize + 16;
    delayLineL.assign ((size_t) maxDelaySamples, 0.0f);
    delayLineR.assign ((size_t) maxDelaySamples, 0.0f);
    delayWritePos = 0;
    smoothedLagSamples = 0.0f;

    // Report half of the worst-case search range as our base latency so the
    // detected lag can swing both negative and positive without ever requiring
    // a negative delay. The host delay-compensates around this stable number.
    // Preallocate the DTW scratch to its worst case (radius <= envWindowSize/2, so
    // W <= envWindowSize + 1) so computeDtwWarp() never allocates on the audio thread.
    dtwCost.assign ((size_t) envWindowSize * (size_t) (envWindowSize + 1), 0.0);
    dtwRev.clear();
    dtwRev.reserve ((size_t) envWindowSize + 4);
    resetDspState();

    baseLatencySamples = (int) std::ceil (0.150 * currentSampleRate);
    setLatencySamples (baseLatencySamples);

    samplesUntilSearch = 0;
    publishedOffsetMs.store (0.0f);
    publishedConfidence.store (0.0f);
    guideHasAudio.store (false);
    locked.store (false);
}

bool SattariAlignAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

// Build half-wave-rectified, smoothed energy difference (rough onset envelope)
// and accumulate into the 200 Hz decimated ring buffers.
void SattariAlignAudioProcessor::updateOnsetEnvelope (const juce::AudioBuffer<float>& mainBuf,
                                                      const juce::AudioBuffer<float>& guideBuf,
                                                      int numSamples)
{
    const int mainCh  = mainBuf.getNumChannels();
    const int guideCh = guideBuf.getNumChannels();
    const bool haveGuide = guideCh > 0;

    float guideEnergyThisBlock = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float mEnergy = 0.0f;
        for (int ch = 0; ch < mainCh; ++ch)
        {
            const float s = mainBuf.getSample (ch, i);
            mEnergy += s * s;
        }
        mEnergy = mEnergy / (float) juce::jmax (1, mainCh);
        const float mFlux = juce::jmax (0.0f, mEnergy - mainPrevEnergy);
        mainPrevEnergy = mEnergy;
        const float mSmoothed = mainSmoother.process (mFlux);

        float gSmoothed = 0.0f;
        if (haveGuide)
        {
            float gEnergy = 0.0f;
            for (int ch = 0; ch < guideCh; ++ch)
            {
                const float s = guideBuf.getSample (ch, i);
                gEnergy += s * s;
            }
            gEnergy = gEnergy / (float) juce::jmax (1, guideCh);
            const float gFlux = juce::jmax (0.0f, gEnergy - guidePrevEnergy);
            guidePrevEnergy = gEnergy;
            gSmoothed = guideSmoother.process (gFlux);
            guideEnergyThisBlock += gEnergy;
        }

        mainEnvAccum  += mSmoothed; ++mainEnvCount;
        guideEnvAccum += gSmoothed; ++guideEnvCount;

        if (++envAccumulator >= envSamplesPerHost)
        {
            envAccumulator = 0;
            mainEnv[(size_t) envWritePos]  = mainEnvCount  > 0 ? mainEnvAccum  / (float) mainEnvCount  : 0.0f;
            guideEnv[(size_t) envWritePos] = guideEnvCount > 0 ? guideEnvAccum / (float) guideEnvCount : 0.0f;
            envWritePos = (envWritePos + 1) % envHistorySize;
            mainEnvAccum = guideEnvAccum = 0.0f;
            mainEnvCount = guideEnvCount = 0;
        }
    }

    guideHasAudio.store (guideEnergyThisBlock > 1.0e-7f);
}

// Search lag (in envelope frames) within ±searchRangeFrames that maximises
// the normalised cross-correlation between the two envelope rings. Returns
// the lag in envelope frames; positive means Main is ahead of Guide.
// Constrained-band DTW between recent Guide and Main envelopes. Builds a
// piecewise-linear warp path of (time, lagSamples) anchors that the audio
// thread interpolates each block. Real Sakoe-Chiba band (radius = search
// frames). Returns 0 on success.
int SattariAlignAudioProcessor::computeDtwWarp()
{
    const float searchRangeMs = apvts.getRawParameterValue ("searchRangeMs")->load();
    const int radius = juce::jlimit (1, envWindowSize / 2,
                                     (int) std::round ((double) searchRangeMs * 0.001 * (double) envSampleRate));
    const int N = envWindowSize;

    auto envAt = [this] (const std::vector<float>& ring, int idx) -> float
    {
        idx = ((idx % envHistorySize) + envHistorySize) % envHistorySize;
        return ring[(size_t) idx];
    };
    const int base = (envWritePos + envHistorySize - envWindowSize) % envHistorySize;

    // 1-D arrays held as flat (i,j) -> cost[(i*(2*radius+1)) + (j-i+radius)].
    const int W = 2 * radius + 1;
    // Reuse the preallocated scratch (sized for the worst-case W in prepareToPlay);
    // only the first N*W entries are used this call. No audio-thread allocation.
    auto& cost = dtwCost;
    std::fill (cost.begin(), cost.begin() + (size_t) N * (size_t) W, std::numeric_limits<double>::infinity());
    auto idx = [W, radius] (int i, int j) -> int { return i * W + (j - i + radius); };

    cost[(size_t) idx (0, 0)] = 0.0;
    for (int i = 1; i < N; ++i)
    {
        const float gi = envAt (guideEnv, base + i);
        const int jLo = juce::jmax (0, i - radius);
        const int jHi = juce::jmin (N - 1, i + radius);
        for (int j = jLo; j <= jHi; ++j)
        {
            const float mj = envAt (mainEnv, base + j);
            const double local = std::abs ((double) gi - (double) mj);
            double best = std::numeric_limits<double>::infinity();
            // Three predecessors: (i-1,j-1) diagonal, (i-1,j) advance guide,
            // (i,j-1) advance main. Sakoe-Chiba band bounds j-i to [-radius, +radius].
            if (j - 1 >= juce::jmax (0, (i - 1) - radius))
                best = juce::jmin (best, cost[(size_t) idx (i - 1, j - 1)]);
            if (std::abs (j - (i - 1)) <= radius)
                best = juce::jmin (best, cost[(size_t) idx (i - 1, j)]);
            if (j - 1 >= juce::jmax (0, i - radius))
                best = juce::jmin (best, cost[(size_t) idx (i, j - 1)]);
            cost[(size_t) idx (i, j)] = best + local;
        }
    }

    // Backtrack from (N-1, N-1) - if it's unreachable, find the best terminal in
    // the last row within band.
    int i = N - 1, j = N - 1;
    {
        double bestEnd = std::numeric_limits<double>::infinity();
        for (int jj = juce::jmax (0, i - radius); jj <= juce::jmin (N - 1, i + radius); ++jj)
        {
            if (cost[(size_t) idx (i, jj)] < bestEnd) { bestEnd = cost[(size_t) idx (i, jj)]; j = jj; }
        }
        if (! std::isfinite (bestEnd)) return 1;
    }

    // Walk back; sample every ~50 ms of guide-time so the path is compact.
    const int stride = juce::jmax (1, (int) (envSampleRate * 0.05));
    auto& rev = dtwRev;   // preallocated; reused each call (no audio-thread alloc)
    rev.clear();
    while (i > 0 && j > 0)
    {
        if ((N - 1 - i) % stride == 0)
            rev.push_back ({ (double) i / (double) envSampleRate,
                             (float) ((double) (j - i) * (double) currentSampleRate / (double) envSampleRate) });
        const double diag = cost[(size_t) idx (i - 1, j - 1)];
        const double upI  = (std::abs (j - (i - 1)) <= radius) ? cost[(size_t) idx (i - 1, j)] : std::numeric_limits<double>::infinity();
        const double left = (j - 1 >= juce::jmax (0, i - radius)) ? cost[(size_t) idx (i, j - 1)] : std::numeric_limits<double>::infinity();
        if (diag <= upI && diag <= left) { --i; --j; }
        else if (upI <= left)            { --i; }
        else                             { --j; }
    }
    rev.push_back ({ 0.0, (float) ((double) (j - i) * (double) currentSampleRate / (double) envSampleRate) });

    // Reverse to forward-time order.
    std::reverse (rev.begin(), rev.end());

    // computeDtwWarp and playback both run serially on the audio thread, so a
    // lock here only adds real-time overhead without protecting shared state.
    warpPath = std::move (rev);
    dtwClockSec = 0.0;
    return 0;
}

int SattariAlignAudioProcessor::searchBestLagFrames (float& outConfidence) const
{
    const float searchRangeMs = apvts.getRawParameterValue ("searchRangeMs")->load();
    const int searchFrames    = juce::jlimit (1, envWindowSize / 2,
                                              (int) std::round ((double) searchRangeMs * 0.001 * (double) envSampleRate));

    auto envAt = [this] (const std::vector<float>& ring, int idx) -> float
    {
        idx = ((idx % envHistorySize) + envHistorySize) % envHistorySize;
        return ring[(size_t) idx];
    };

    // Reference: the most recent `envWindowSize` of Guide; we shift Main against it.
    const int base = (envWritePos + envHistorySize - envWindowSize) % envHistorySize;

    // Precompute Guide stats.
    double gSum = 0.0, gSqSum = 0.0;
    for (int i = 0; i < envWindowSize; ++i)
    {
        const float g = envAt (guideEnv, base + i);
        gSum += g; gSqSum += (double) g * g;
    }
    const double gMean = gSum / (double) envWindowSize;
    const double gVar  = juce::jmax (1.0e-9, gSqSum / (double) envWindowSize - gMean * gMean);

    int   bestLag = 0;
    double bestScore = -2.0;
    for (int lag = -searchFrames; lag <= searchFrames; ++lag)
    {
        double mSum = 0.0, mSqSum = 0.0, mgSum = 0.0;
        for (int i = 0; i < envWindowSize; ++i)
        {
            const float g = envAt (guideEnv, base + i);
            const float m = envAt (mainEnv,  base + i + lag);
            mSum   += m;
            mSqSum += (double) m * m;
            mgSum  += (double) m * g;
        }
        const double mMean = mSum / (double) envWindowSize;
        const double mVar  = juce::jmax (1.0e-9, mSqSum / (double) envWindowSize - mMean * mMean);
        const double cov   = mgSum / (double) envWindowSize - mMean * gMean;
        const double corr  = cov / std::sqrt (mVar * gVar);
        if (corr > bestScore)
        {
            bestScore = corr;
            bestLag = lag;
        }
    }

    outConfidence = (float) juce::jlimit (0.0, 1.0, bestScore);
    return bestLag;
}

void SattariAlignAudioProcessor::resetDspState()
{
    std::fill (mainEnv.begin(), mainEnv.end(), 0.0f);
    std::fill (guideEnv.begin(), guideEnv.end(), 0.0f);
    std::fill (delayLineL.begin(), delayLineL.end(), 0.0f);
    std::fill (delayLineR.begin(), delayLineR.end(), 0.0f);
    delayWritePos = 0;
    envWritePos = 0;
    envAccumulator = 0;
    mainEnvAccum = guideEnvAccum = 0.0f;
    mainEnvCount = guideEnvCount = 0;
    mainPrevEnergy = guidePrevEnergy = 0.0f;
    smoothedLagSamples = 0.0f;
}

void SattariAlignAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    auto mainBuffer = getBusBuffer (buffer, true, 0);
    auto outBuffer  = getBusBuffer (buffer, false, 0);
    const int numChannels = juce::jmin (mainBuffer.getNumChannels(), outBuffer.getNumChannels());
    if (numChannels <= 0) return;

    juce::AudioBuffer<float> guideBuffer;
    if (getBusCount (true) > 1)
        if (auto* sc = getBus (true, 1); sc != nullptr && sc->isEnabled())
            guideBuffer = getBusBuffer (buffer, true, 1);

    // True bypass: skip all alignment work but keep the reported base latency so the
    // host's PDC chain stays sample-aligned. Slew the alignment lag back to zero so
    // toggling bypass doesn't click, then fall through to the shared delay-output loop.
    const bool bypassed = apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    if (bypassed)
    {
        smoothedLagSamples += 0.02f * (0.0f - smoothedLagSamples);
    }
    else
    {
    const bool autoAlign     = apvts.getRawParameterValue ("autoAlign")->load() > 0.5f;
    const bool lockOn        = apvts.getRawParameterValue ("lock")->load() > 0.5f;
    const float manualMs     = apvts.getRawParameterValue ("manualOffsetMs")->load();
    const float sensitivity  = apvts.getRawParameterValue ("sensitivity")->load();
    const float smoothing    = apvts.getRawParameterValue ("smoothing")->load();
    locked.store (lockOn);

    updateOnsetEnvelope (mainBuffer, guideBuffer, numSamples);

    const int algorithm = juce::roundToInt (apvts.getRawParameterValue ("algorithm")->load());
    const bool useDtw = (algorithm == 1);

    samplesUntilSearch -= numSamples;
    if (samplesUntilSearch <= 0 && autoAlign && ! lockOn && guideHasAudio.load())
    {
        if (useDtw)
        {
            // v2 alpha: full DTW. Path drives the per-sample interpolation below.
            computeDtwWarp();
            // Confidence reuses the cross-corr peak as a proxy (cheaper than
            // computing path cost normalisation here on the audio thread).
            float conf = 0.0f;
            (void) searchBestLagFrames (conf);
            publishedConfidence.store (conf);
        }
        else
        {
            float conf = 0.0f;
            const int lagFrames = searchBestLagFrames (conf);
            const double lagSec = (double) lagFrames / (double) envSampleRate;
            const float  lagSamples = (float) (lagSec * currentSampleRate);
            publishedConfidence.store (conf);
            if (conf >= sensitivity * 0.6f)
            {
                const float alpha = 1.0f - juce::jlimit (0.0f, 0.99f, smoothing);
                const float blend = juce::jmax (alpha, 0.05f);
                smoothedLagSamples += blend * (lagSamples - smoothedLagSamples);
            }
        }
        samplesUntilSearch = (int) (0.2 * currentSampleRate);
    }
    else if (! autoAlign || lockOn)
    {
        const float target = (float) (manualMs * 0.001 * currentSampleRate);
        const float blend  = 0.02f;
        smoothedLagSamples += blend * (target - smoothedLagSamples);
    }

    // For DTW, the published "offset" is the lag at the current clock; for
    // bulk-shift, the smoothed lag.
    if (useDtw)
    {
        float lagNow = smoothedLagSamples;
        if (warpPath.size() >= 2)
        {
            // Find segment around dtwClockSec.
            const double t = dtwClockSec;
            int hi = 1;
            while (hi < (int) warpPath.size() && warpPath[(size_t) hi].timeSec < t) ++hi;
            hi = juce::jmin (hi, (int) warpPath.size() - 1);
            const int lo = juce::jmax (0, hi - 1);
            const double t0 = warpPath[(size_t) lo].timeSec;
            const double t1 = warpPath[(size_t) hi].timeSec;
            const double f  = (t1 > t0) ? juce::jlimit (0.0, 1.0, (t - t0) / (t1 - t0)) : 0.0;
            lagNow = (float) ((1.0 - f) * warpPath[(size_t) lo].lagSamples
                               + f         * warpPath[(size_t) hi].lagSamples);
        }
        // Smooth into the applied lag so the warp path's piecewise jumps don't
        // click. The user's smoothing knob still controls how slewy this is.
        const float alpha = 1.0f - juce::jlimit (0.0f, 0.995f, smoothing);
        smoothedLagSamples += alpha * (lagNow - smoothedLagSamples);
        dtwClockSec += (double) numSamples / currentSampleRate;
    }

    publishedOffsetMs.store ((float) (smoothedLagSamples / currentSampleRate * 1000.0));
    } // end alignment work (skipped when bypassed)

    // Apply the smoothed delay to the Main output. Total delay = baseLatency +
    // smoothedLagSamples (clamped to the line size).
    const float delayF = juce::jlimit (0.0f,
                                       (float) (maxDelaySamples - 2),
                                       (float) baseLatencySamples + smoothedLagSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        // Write input -> delay line.
        const float inL = mainBuffer.getSample (0, i);
        const float inR = numChannels > 1 ? mainBuffer.getSample (1, i) : inL;
        delayLineL[(size_t) delayWritePos] = inL;
        delayLineR[(size_t) delayWritePos] = inR;

        // Read with linear interpolation at the fractional delay.
        float readPos = (float) delayWritePos - delayF;
        while (readPos < 0.0f) readPos += (float) maxDelaySamples;
        const int   i0 = ((int) readPos) % maxDelaySamples;
        const int   i1 = (i0 + 1) % maxDelaySamples;
        const float frac = readPos - std::floor (readPos);

        const float outL = delayLineL[(size_t) i0] * (1.0f - frac) + delayLineL[(size_t) i1] * frac;
        const float outR = delayLineR[(size_t) i0] * (1.0f - frac) + delayLineR[(size_t) i1] * frac;

        outBuffer.setSample (0, i, outL);
        if (numChannels > 1)
            outBuffer.setSample (1, i, outR);

        if (++delayWritePos >= maxDelaySamples) delayWritePos = 0;
    }
}

SattariAlignAudioProcessor::LiveSnapshot SattariAlignAudioProcessor::getLiveSnapshot() const noexcept
{
    LiveSnapshot s;
    s.detectedOffsetMs = publishedOffsetMs.load();
    s.confidence       = publishedConfidence.load();
    s.guideActive      = guideHasAudio.load();
    s.locked           = locked.load();
    return s;
}

juce::AudioProcessorEditor* SattariAlignAudioProcessor::createEditor()
{
    return new SattariAlignAudioProcessorEditor (*this);
}

void SattariAlignAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SattariAlignAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariAlignAudioProcessor();
}
