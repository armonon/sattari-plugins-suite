#include "DeckPlayer.h"
#include "Interpolation.h"

juce::String DeckPlayer::stemLaneId (int laneIndex)
{
    switch (juce::jlimit (0, numStemLanes - 1, laneIndex))
    {
        case fullMixStem: return "Full";
        case drumsStem:   return "Drums";
        case bassStem:    return "Bass";
        case musicStem:   return "Music";
        case vocalsStem:  return "Vocals";
        default:          break;
    }

    return "Stem";
}

juce::String DeckPlayer::stemLaneLabel (int laneIndex)
{
    switch (juce::jlimit (0, numStemLanes - 1, laneIndex))
    {
        case fullMixStem: return "Full mix";
        case drumsStem:   return "Drums";
        case bassStem:    return "Bass";
        case musicStem:   return "Music";
        case vocalsStem:  return "Vocals";
        default:          break;
    }

    return "Stem";
}

void DeckPlayer::prepare (double sampleRate, int maxBlockSize, int outputChannels)
{
    currentSampleRate = sampleRate;
    preparedMaxBlockSize = juce::jmax (1, maxBlockSize);
    channels = juce::jmax (1, outputChannels);
    eq.prepare (sampleRate, outputChannels);
    filter.prepare (sampleRate);

#if SATTARI_STEMDECK_HAS_RUBBERBAND
    const int maxRubberInputFrames = juce::jmax (preparedMaxBlockSize * 8, preparedMaxBlockSize + 8192);
    rubberBandInput.setSize (channels, maxRubberInputFrames);
    rubberBandOutput.setSize (channels, preparedMaxBlockSize);
    rubberBandInputPointers.resize ((size_t) channels);
    rubberBandOutputPointers.resize ((size_t) channels);
    rubberBand = std::make_unique<RubberBand::RubberBandStretcher> (
        currentSampleRate,
        (size_t) channels,
        RubberBand::RubberBandStretcher::OptionProcessRealTime
          | RubberBand::RubberBandStretcher::OptionEngineFiner
          | RubberBand::RubberBandStretcher::OptionPitchHighConsistency
          | RubberBand::RubberBandStretcher::OptionChannelsTogether);
    rubberBand->setMaxProcessSize ((size_t) maxRubberInputFrames);
    resetRubberBand();
#endif
}

void DeckPlayer::reset()
{
    playheadSamples = 0.0;
    publicPlayheadSamples.store (0.0, std::memory_order_relaxed);
    eq.reset();
    filter.reset();
#if SATTARI_STEMDECK_HAS_RUBBERBAND
    resetRubberBand();
#endif
}

void DeckPlayer::setLoadedAudio (std::shared_ptr<const LoadedAudio> newAudio)
{
    setStemAudio (fullMixStem, std::move (newAudio));
    seekStart();
}

std::shared_ptr<const DeckPlayer::LoadedAudio> DeckPlayer::getLoadedAudio() const
{
    return getStemAudio (fullMixStem);
}

void DeckPlayer::setStemAudio (int laneIndex, std::shared_ptr<const LoadedAudio> newAudio)
{
    if (! juce::isPositiveAndBelow (laneIndex, numStemLanes))
        return;

    std::atomic_store (&loadedAudio[(size_t) laneIndex], std::move (newAudio));
}

std::shared_ptr<const DeckPlayer::LoadedAudio> DeckPlayer::getStemAudio (int laneIndex) const
{
    return juce::isPositiveAndBelow (laneIndex, numStemLanes)
        ? std::atomic_load (&loadedAudio[(size_t) laneIndex])
        : std::shared_ptr<const LoadedAudio>{};
}

juce::String DeckPlayer::getPath() const
{
    auto audio = getLoadedAudio();
    return audio ? audio->path : juce::String{};
}

juce::String DeckPlayer::getStemPath (int laneIndex) const
{
    auto audio = getStemAudio (laneIndex);
    return audio ? audio->path : juce::String{};
}

void DeckPlayer::seekStart()
{
    playheadSamples = 0.0;
    publicPlayheadSamples.store (0.0, std::memory_order_relaxed);
#if SATTARI_STEMDECK_HAS_RUBBERBAND
    rubberBandSourceReadSamples = 0.0;
    resetRubberBand();
#endif
}

void DeckPlayer::seekToSamples (double referenceSamples)
{
    pendingSeekSamples.store (juce::jmax (0.0, referenceSamples), std::memory_order_relaxed);
}

void DeckPlayer::setLoopRegion (double inSamples, double outSamples)
{
    loopInRefSamples.store  (juce::jmax (0.0, inSamples),  std::memory_order_relaxed);
    loopOutRefSamples.store (juce::jmax (0.0, outSamples), std::memory_order_relaxed);
}

void DeckPlayer::clearLoopRegion()
{
    loopActiveFlag.store    (false, std::memory_order_relaxed);
    loopInRefSamples.store  (-1.0, std::memory_order_relaxed);
    loopOutRefSamples.store (-1.0, std::memory_order_relaxed);
}

void DeckPlayer::setLoopActive (bool active)
{
    loopActiveFlag.store (active, std::memory_order_relaxed);
}

void DeckPlayer::beginLoopRoll (double loopLengthSeconds) noexcept
{
    const double pos = publicPlayheadSamples.load (std::memory_order_relaxed);
    // The loop region lives in reference (source) sample coordinates — same space
    // as setDeckLoopSeconds — so the length must convert with the source SR, not
    // the device SR, otherwise the roll is off-length when device SR != source SR.
    auto audio = getLoadedAudio();
    const double refSr = (audio != nullptr && audio->sourceSampleRate > 0.0) ? audio->sourceSampleRate
                                                                             : currentSampleRate;
    const double loopLenSamples = loopLengthSeconds * refSr;
    setLoopRegion (pos, pos + loopLenSamples);
    setLoopActive (true);
    setSlipEnabled (true);
}

void DeckPlayer::endLoopRoll() noexcept
{
    setLoopActive (false);
    setSlipEnabled (false);   // triggers snap-back to shadow position in process()
}

double DeckPlayer::getPlayheadReferenceSamples() const noexcept
{
    return publicPlayheadSamples.load (std::memory_order_relaxed);
}

TrackAnalysis DeckPlayer::analyzeFullMixAroundPlayhead (double windowSeconds) const
{
    auto audio = getLoadedAudio();
    if (! audio || audio->audio.getNumSamples() <= 1 || audio->sourceSampleRate <= 0.0)
        return {};

    std::array<std::shared_ptr<const LoadedAudio>, numStemLanes> laneAudio;
    for (int lane = 0; lane < numStemLanes; ++lane)
        laneAudio[(size_t) lane] = getStemAudio (lane);

    const double referenceSampleRate = referenceSampleRateFor (laneAudio);
    const double fullMixPosition = getPlayheadReferenceSamples() * audio->sourceSampleRate / referenceSampleRate;
    const int windowSamples = juce::jlimit (4096, audio->audio.getNumSamples(),
                                           (int) std::round (juce::jlimit (2.0, 24.0, windowSeconds) * audio->sourceSampleRate));
    const int halfWindow = windowSamples / 2;
    const int startSample = juce::jlimit (0, juce::jmax (0, audio->audio.getNumSamples() - windowSamples),
                                         (int) std::round (fullMixPosition) - halfWindow);

    juce::AudioBuffer<float> window (audio->audio.getNumChannels(), windowSamples);
    for (int ch = 0; ch < window.getNumChannels(); ++ch)
        window.copyFrom (ch, 0, audio->audio, ch, startSample, windowSamples);

    return TrackAnalyzer::analyze (window, audio->sourceSampleRate);
}

float DeckPlayer::readInterpolated (const juce::AudioBuffer<float>& audio, int channel, double position)
{
    // 16-tap Blackman-windowed sinc (see Interpolation.h): near-transparent
    // sample-rate conversion and granular reads, and still bit-exact when the
    // position lands on an integer sample (sinc(0) is a unit delta), so
    // matched-rate no-stretch playback remains a pass-through.
    return stemdeck::readSinc (audio, channel, position);
}

double DeckPlayer::wrapPosition (double position, double length)
{
    if (length <= 1.0)
        return 0.0;

    auto wrapped = std::fmod (position, length);
    if (wrapped < 0.0)
        wrapped += length;
    return wrapped;
}

float DeckPlayer::hannWindow (double phase, double size)
{
    if (size <= 1.0 || phase < 0.0 || phase >= size)
        return 0.0f;

    return (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * phase / (size - 1.0)));
}

double DeckPlayer::referenceSampleRateFor (const std::array<std::shared_ptr<const LoadedAudio>, numStemLanes>& laneAudio)
{
    for (const auto& audio : laneAudio)
        if (audio && audio->sourceSampleRate > 0.0 && audio->audio.getNumSamples() > 1)
            return audio->sourceSampleRate;

    return 44100.0;
}

double DeckPlayer::longestReferenceLengthFor (const std::array<std::shared_ptr<const LoadedAudio>, numStemLanes>& laneAudio,
                                             double referenceSampleRate)
{
    double longest = 0.0;
    for (const auto& audio : laneAudio)
    {
        if (! audio || audio->audio.getNumSamples() <= 1 || audio->sourceSampleRate <= 0.0)
            continue;

        const double lengthInReferenceSamples = (double) audio->audio.getNumSamples() * referenceSampleRate / audio->sourceSampleRate;
        longest = juce::jmax (longest, lengthInReferenceSamples);
    }

    return longest;
}

#if SATTARI_STEMDECK_HAS_RUBBERBAND
void DeckPlayer::resetRubberBand()
{
    if (rubberBand)
        rubberBand->reset();

    rubberBandNeedsReset = false;
    rubberBandLastTempoRatio = -1.0;
    rubberBandLastPitchRatio = -1.0;
    rubberBandInput.clear();
    rubberBandOutput.clear();
}

void DeckPlayer::fillRubberBandInput (int numFrames,
                                      const std::array<std::shared_ptr<const LoadedAudio>, numStemLanes>& laneAudio,
                                      double referenceSampleRate,
                                      double sourceLength,
                                      const std::array<float, numStemLanes>& stemLaneGains)
{
    const int frames = juce::jlimit (0, rubberBandInput.getNumSamples(), numFrames);
    rubberBandInput.clear (0, frames);

    const double referenceStep = referenceSampleRate / currentSampleRate;

    for (int i = 0; i < frames; ++i)
    {
        const double sourceReferencePosition = wrapPosition (rubberBandSourceReadSamples, sourceLength);

        for (int lane = 0; lane < numStemLanes; ++lane)
        {
            const auto& laneData = laneAudio[(size_t) lane];
            if (! laneData || laneData->audio.getNumSamples() <= 1)
                continue;

            const float laneGain = juce::jlimit (0.0f, 2.0f, stemLaneGains[(size_t) lane]);   // linear (see StemGain.h)
            if (laneGain <= 0.00001f)
                continue;

            const double lanePosition = wrapPosition (sourceReferencePosition * laneData->sourceSampleRate / referenceSampleRate,
                                                      (double) laneData->audio.getNumSamples());

            const float left = laneGain * readInterpolated (laneData->audio, 0, lanePosition);
            const float right = laneGain * readInterpolated (laneData->audio, laneData->audio.getNumChannels() > 1 ? 1 : 0, lanePosition);

            rubberBandInput.addSample (0, i, left);
            if (channels > 1)
                rubberBandInput.addSample (1, i, right);
            for (int ch = 2; ch < channels; ++ch)
                rubberBandInput.addSample (ch, i, 0.5f * (left + right));
        }

        rubberBandSourceReadSamples += referenceStep;
        if (rubberBandSourceReadSamples >= sourceLength)
            rubberBandSourceReadSamples = wrapPosition (rubberBandSourceReadSamples, sourceLength);
    }
}

bool DeckPlayer::renderWithRubberBand (juce::AudioBuffer<float>& destination,
                                       int startSample,
                                       int numSamples,
                                       float linearGain,
                                       float lowDb,
                                       float midDb,
                                       float highDb,
                                       float filterControl,
                                       const TimePitchDecision& timePitch,
                                       const std::array<float, numStemLanes>& stemLaneGains,
                                       const BeatSyncEngine::PlaybackPlan& beatSyncPlan,
                                       const std::array<std::shared_ptr<const LoadedAudio>, numStemLanes>& laneAudio,
                                       double referenceSampleRate,
                                       double sourceLength)
{
    if (! rubberBand || ! timePitch.pitchPreservingActive || numSamples <= 0)
        return false;

    // Publish this path's latency so the host can keep key-locked decks phase-aligned
    // with un-key-locked ones (see MainPluginProcessor::refreshLatencyCompensation).
    timeStretchLatency.store ((int) rubberBand->getLatency(), std::memory_order_relaxed);

    const double tempoRatio = juce::jlimit (0.25, 4.0, (double) timePitch.tempoRatio);
    const double timeRatio = juce::jlimit (0.25, 4.0, 1.0 / tempoRatio);
    const double pitchRatio = juce::jlimit (0.25, 4.0, (double) timePitch.pitchRatio);

    // Consume any pending user seek here too — the RubberBand path returns before
    // the granular seek-consume block, so otherwise a hot-cue/beat jump would be
    // dropped and fire stale later. seekTarget is in reference samples.
    {
        const double seekTarget = pendingSeekSamples.exchange (-1.0, std::memory_order_relaxed);
        if (seekTarget >= 0.0)
        {
            rubberBandSourceReadSamples = wrapPosition (seekTarget, sourceLength);
            resetRubberBand();
            if (slipShadowSamples >= 0.0)
                slipShadowSamples = seekTarget;
        }
    }

    if (beatSyncPlan.syncEnabled && beatSyncPlan.hasLockedSourcePosition)
    {
        const double locked = wrapPosition (beatSyncPlan.lockedSourcePositionSamples, sourceLength);
        const double delta = std::abs (locked - wrapPosition (rubberBandSourceReadSamples, sourceLength));
        const double wrappedDelta = juce::jmin (delta, sourceLength - delta);
        if (wrappedDelta > referenceSampleRate * 0.05)
        {
            rubberBandSourceReadSamples = locked;
            resetRubberBand();
        }
    }

    if (rubberBandNeedsReset)
        resetRubberBand();

    if (std::abs (timeRatio - rubberBandLastTempoRatio) > 0.0001)
    {
        rubberBand->setTimeRatio (timeRatio);
        rubberBandLastTempoRatio = timeRatio;
    }

    if (std::abs (pitchRatio - rubberBandLastPitchRatio) > 0.0001)
    {
        rubberBand->setPitchScale (pitchRatio);
        rubberBandLastPitchRatio = pitchRatio;
    }

    for (int ch = 0; ch < channels; ++ch)
        rubberBandOutputPointers[(size_t) ch] = rubberBandOutput.getWritePointer (ch);

    int available = rubberBand->available();
    int safety = 0;
    while (available < numSamples && safety++ < 8)
    {
        const int requested = (int) rubberBand->getSamplesRequired();
        const int framesToFeed = juce::jlimit (1, rubberBandInput.getNumSamples(), requested > 0 ? requested : preparedMaxBlockSize);
        fillRubberBandInput (framesToFeed, laneAudio, referenceSampleRate, sourceLength, stemLaneGains);

        for (int ch = 0; ch < channels; ++ch)
            rubberBandInputPointers[(size_t) ch] = rubberBandInput.getReadPointer (ch);

        rubberBand->process (rubberBandInputPointers.data(), (size_t) framesToFeed, false);
        available = rubberBand->available();
        if (available < 0)
            break;
    }

    rubberBandOutput.clear();
    const int framesRetrieved = available > 0
        ? (int) rubberBand->retrieve (rubberBandOutputPointers.data(), (size_t) juce::jmin (available, numSamples))
        : 0;

    for (int i = 0; i < numSamples; ++i)
    {
        float left = i < framesRetrieved ? rubberBandOutput.getSample (0, i) : 0.0f;
        float right = i < framesRetrieved ? rubberBandOutput.getSample (channels > 1 ? 1 : 0, i) : left;

        eq.processSample (left, right, lowDb, midDb, highDb);
        filter.processSample (left, right, filterControl);

        left *= linearGain;
        right *= linearGain;

        const int outIndex = startSample + i;
        if (destination.getNumChannels() == 1)
        {
            destination.addSample (0, outIndex, 0.5f * (left + right));
        }
        else
        {
            destination.addSample (0, outIndex, left);
            destination.addSample (1, outIndex, right);
            for (int ch = 2; ch < destination.getNumChannels(); ++ch)
                destination.addSample (ch, outIndex, 0.5f * (left + right));
        }
    }

    playheadSamples = wrapPosition (rubberBandSourceReadSamples, sourceLength);
    publicPlayheadSamples.store (playheadSamples, std::memory_order_relaxed);
    return true;
}
#endif

void DeckPlayer::process (juce::AudioBuffer<float>& destination,
                          int startSample,
                          int numSamples,
                          bool shouldPlay,
                          float trimGainDb,
                          float channelFader,
                          float lowDb,
                          float midDb,
                          float highDb,
                          float filterControl,
                          float deckMixGain,
                          const TimePitchDecision& timePitch,
                          const std::array<float, numStemLanes>& stemLaneGains,
                          const std::array<float, numStemLanes>& stemPitchSemitones,
                          const BeatSyncEngine::PlaybackPlan& beatSyncPlan)
{
    std::array<std::shared_ptr<const LoadedAudio>, numStemLanes> laneAudio;
    bool hasAnyAudio = false;
    for (int lane = 0; lane < numStemLanes; ++lane)
    {
        laneAudio[(size_t) lane] = getStemAudio (lane);
        hasAnyAudio = hasAnyAudio || (laneAudio[(size_t) lane] && laneAudio[(size_t) lane]->audio.getNumSamples() > 1);
    }

    if (! shouldPlay || ! hasAnyAudio)
        return;

    const float linearGain = juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 6.0f, trimGainDb))
                           * juce::jlimit (0.0f, 1.0f, channelFader)
                           * deckMixGain;
    if (linearGain <= 0.00001f)
        return;

    const double referenceSampleRate = referenceSampleRateFor (laneAudio);
    const double sourceLength = longestReferenceLengthFor (laneAudio, referenceSampleRate);
    if (sourceLength <= 1.0)
        return;

    const double outputToReference = referenceSampleRate / currentSampleRate;
    const double resampleRatio = outputToReference * juce::jlimit (0.125f, 8.0f, timePitch.sourceRateMultiplier);
    const double tempoRatio = juce::jlimit (0.25, 4.0, (double) timePitch.tempoRatio);
    const double pitchRatio = juce::jlimit (0.25, 4.0, (double) timePitch.pitchRatio);

    // Independent per-stem pitch: each lane resamples at its own ratio. Any non-zero
    // offset forces the pitch-preserving granular path (RubberBand can't split stems).
    std::array<double, numStemLanes> lanePitchRatio;
    bool anyStemPitch = false;
    for (int lane = 0; lane < numStemLanes; ++lane)
    {
        const float semis = juce::jlimit (-12.0f, 12.0f, stemPitchSemitones[(size_t) lane]);
        if (std::abs (semis) > 0.01f) anyStemPitch = true;
        lanePitchRatio[(size_t) lane] = pitchRatio * std::pow (2.0, (double) semis / 12.0);
    }
    const bool useGranularKeyLock = timePitch.pitchPreservingActive || anyStemPitch;
    const double granularLoopLength = juce::jmax (1.0, sourceLength / tempoRatio);
    const double grainSize = juce::jlimit (512.0, 4096.0, 2048.0 * referenceSampleRate / 44100.0);
    const double grainHop = grainSize * 0.5;

#if SATTARI_STEMDECK_HAS_RUBBERBAND
    if (useGranularKeyLock && ! anyStemPitch && renderWithRubberBand (destination,
                                                    startSample,
                                                    numSamples,
                                                    linearGain,
                                                    lowDb,
                                                    midDb,
                                                    highDb,
                                                    filterControl,
                                                    timePitch,
                                                    stemLaneGains,
                                                    beatSyncPlan,
                                                    laneAudio,
                                                    referenceSampleRate,
                                                    sourceLength))
        return;
#endif

    // Not on the RubberBand path this block -> no time-stretch latency to compensate.
    timeStretchLatency.store (0, std::memory_order_relaxed);

    // Consume any pending user seek (hot cue jump, beat jump, cue set).
    // seekTarget is in reference (source) samples. Under granular key-lock the
    // playhead runs in loop-output coordinates (source ≈ playhead × tempoRatio,
    // see the grain read below), so convert the same way line ~503 does.
    {
        const double seekTarget = pendingSeekSamples.exchange (-1.0, std::memory_order_relaxed);
        if (seekTarget >= 0.0)
        {
            playheadSamples = useGranularKeyLock ? seekTarget / tempoRatio : seekTarget;
            // A seek while slip is active resets the shadow to the new position
            if (slipShadowSamples >= 0.0)
                slipShadowSamples = seekTarget;
        }
    }

    if (beatSyncPlan.syncEnabled && beatSyncPlan.hasLockedSourcePosition)
    {
        const double sourceReferencePosition = wrapPosition (beatSyncPlan.lockedSourcePositionSamples, sourceLength);
        playheadSamples = useGranularKeyLock ? sourceReferencePosition / tempoRatio : sourceReferencePosition;
    }

    const double loopIn  = loopInRefSamples.load  (std::memory_order_relaxed);
    const double loopOut = loopOutRefSamples.load  (std::memory_order_relaxed);
    const bool   hasLoop = loopActiveFlag.load (std::memory_order_relaxed)
                        && loopIn >= 0.0 && loopOut > loopIn + 64.0;

    // Slip mode: shadow playhead advances at 1× real-time regardless of loop state.
    // On slip-off, queue a seek to the shadow position so playback snaps back.
    const bool slipNow = slipActive.load (std::memory_order_relaxed);
    if (slipNow && ! slipWasActive)
    {
        // Slip just enabled — initialize shadow at current position (reference SR)
        slipShadowSamples = playheadSamples;
        slipWasActive = true;
    }
    else if (! slipNow && slipWasActive)
    {
        // Slip just disabled — snap main playhead to where we would have been
        if (slipShadowSamples >= 0.0)
            pendingSeekSamples.store (slipShadowSamples, std::memory_order_relaxed);
        slipShadowSamples = -1.0;
        slipWasActive = false;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        // Advance shadow playhead at 1× speed (outputToReference = ref/output sample rate)
        if (slipNow && slipShadowSamples >= 0.0)
        {
            slipShadowSamples += outputToReference;
            if (slipShadowSamples >= sourceLength)
                slipShadowSamples = wrapPosition (slipShadowSamples, sourceLength);
        }

        if (hasLoop && ! useGranularKeyLock)
        {
            if (playheadSamples >= loopOut)
                playheadSamples = loopIn + std::fmod (playheadSamples - loopIn, loopOut - loopIn);
        }
        else
        {
            const double loopLength = useGranularKeyLock ? granularLoopLength : sourceLength;
            if (playheadSamples >= loopLength)
                playheadSamples = wrapPosition (playheadSamples, loopLength);
        }

        float left = 0.0f;
        float right = 0.0f;

        if (useGranularKeyLock)
        {
            const double grainBase = std::floor (playheadSamples / grainHop);
            float weightSum = 0.0f;

            for (int overlap = 0; overlap < 2; ++overlap)
            {
                const double grainIndex = grainBase - (double) overlap;
                if (grainIndex < 0.0)
                    continue;

                const double grainOutputStart = grainIndex * grainHop;
                const double phase = playheadSamples - grainOutputStart;
                const float grainWeight = hannWindow (phase, grainSize);
                if (grainWeight <= 0.0f)
                    continue;

                weightSum += grainWeight;

                for (int lane = 0; lane < numStemLanes; ++lane)
                {
                    const auto& laneData = laneAudio[(size_t) lane];
                    if (! laneData || laneData->audio.getNumSamples() <= 1)
                        continue;

                    const float laneGain = juce::jlimit (0.0f, 2.0f, stemLaneGains[(size_t) lane]);   // linear (see StemGain.h)
                    if (laneGain <= 0.00001f)
                        continue;

                    // Each lane reads at its own pitch ratio (independent stem pitch).
                    const double sourceReferencePosition = wrapPosition (grainIndex * grainHop * tempoRatio
                                                                         + phase * lanePitchRatio[(size_t) lane], sourceLength);
                    const double laneLength = (double) laneData->audio.getNumSamples();
                    const double lanePosition = wrapPosition (sourceReferencePosition * laneData->sourceSampleRate / referenceSampleRate, laneLength);
                    left += grainWeight * laneGain * readInterpolated (laneData->audio, 0, lanePosition);
                    right += grainWeight * laneGain * readInterpolated (laneData->audio, laneData->audio.getNumChannels() > 1 ? 1 : 0, lanePosition);
                }
            }

            if (weightSum > 0.00001f)
            {
                left /= weightSum;
                right /= weightSum;
            }
        }
        else
        {
            for (int lane = 0; lane < numStemLanes; ++lane)
            {
                const auto& laneData = laneAudio[(size_t) lane];
                if (! laneData || laneData->audio.getNumSamples() <= 1)
                    continue;

                const float laneGain = juce::jlimit (0.0f, 2.0f, stemLaneGains[(size_t) lane]);   // linear (see StemGain.h)
                if (laneGain <= 0.00001f)
                    continue;

                const double lanePosition = wrapPosition (playheadSamples * laneData->sourceSampleRate / referenceSampleRate,
                                                          (double) laneData->audio.getNumSamples());
                left += laneGain * readInterpolated (laneData->audio, 0, lanePosition);
                right += laneGain * readInterpolated (laneData->audio, laneData->audio.getNumChannels() > 1 ? 1 : 0, lanePosition);
            }
        }

        eq.processSample (left, right, lowDb, midDb, highDb);
        filter.processSample (left, right, filterControl);

        left *= linearGain;
        right *= linearGain;

        const int outIndex = startSample + i;
        if (destination.getNumChannels() == 1)
        {
            destination.addSample (0, outIndex, 0.5f * (left + right));
        }
        else
        {
            destination.addSample (0, outIndex, left);
            destination.addSample (1, outIndex, right);
            for (int ch = 2; ch < destination.getNumChannels(); ++ch)
                destination.addSample (ch, outIndex, 0.5f * (left + right));
        }

        playheadSamples += useGranularKeyLock ? outputToReference : resampleRatio;
    }

    publicPlayheadSamples.store (playheadSamples, std::memory_order_relaxed);
}
