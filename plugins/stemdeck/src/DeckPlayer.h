#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "BeatSyncEngine.h"
#include "EQProcessor.h"
#include "FilterProcessor.h"
#include "TimePitchEngine.h"
#include "TrackAnalysis.h"

#ifndef SATTARI_STEMDECK_HAS_RUBBERBAND
#define SATTARI_STEMDECK_HAS_RUBBERBAND 0
#endif

#if SATTARI_STEMDECK_HAS_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

class DeckPlayer
{
public:
    static constexpr int numStemLanes = 5;

    enum StemLane
    {
        fullMixStem = 0,
        drumsStem,
        bassStem,
        musicStem,
        vocalsStem
    };

    struct LoadedAudio
    {
        juce::AudioBuffer<float> audio;
        double sourceSampleRate = 44100.0;
        juce::String path;
        juce::String displayName;
        TrackAnalysis analysis;
    };

    static juce::String stemLaneId (int laneIndex);
    static juce::String stemLaneLabel (int laneIndex);

    void prepare (double sampleRate, int maxBlockSize, int outputChannels);
    void reset();
    void setLoadedAudio (std::shared_ptr<const LoadedAudio> newAudio);
    std::shared_ptr<const LoadedAudio> getLoadedAudio() const;
    void setStemAudio (int laneIndex, std::shared_ptr<const LoadedAudio> newAudio);
    std::shared_ptr<const LoadedAudio> getStemAudio (int laneIndex) const;
    juce::String getPath() const;
    juce::String getStemPath (int laneIndex) const;
    void seekStart();
    void seekToSamples (double referenceSamples);   // thread-safe — queued for next process()
    double getPlayheadReferenceSamples() const noexcept;
    TrackAnalysis analyzeFullMixAroundPlayhead (double windowSeconds) const;

    // Loop region (reference-sample coordinates, same rate as playhead)
    void setLoopRegion (double inSamples, double outSamples);
    void clearLoopRegion();
    void setLoopActive (bool active);
    bool isLoopActive() const noexcept { return loopActiveFlag.load (std::memory_order_relaxed); }
    double getLoopInSamples()  const noexcept { return loopInRefSamples.load  (std::memory_order_relaxed); }
    double getLoopOutSamples() const noexcept { return loopOutRefSamples.load (std::memory_order_relaxed); }

    void process (juce::AudioBuffer<float>& destination,
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
                  const BeatSyncEngine::PlaybackPlan& beatSyncPlan);

private:
    static float readInterpolated (const juce::AudioBuffer<float>& audio, int channel, double position);
    static double wrapPosition (double position, double length);
    static float hannWindow (double phase, double size);
    static double referenceSampleRateFor (const std::array<std::shared_ptr<const LoadedAudio>, numStemLanes>& laneAudio);
    static double longestReferenceLengthFor (const std::array<std::shared_ptr<const LoadedAudio>, numStemLanes>& laneAudio, double referenceSampleRate);

#if SATTARI_STEMDECK_HAS_RUBBERBAND
    bool renderWithRubberBand (juce::AudioBuffer<float>& destination,
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
                               double sourceLength);
    void resetRubberBand();
    void fillRubberBandInput (int numFrames,
                              const std::array<std::shared_ptr<const LoadedAudio>, numStemLanes>& laneAudio,
                              double referenceSampleRate,
                              double sourceLength,
                              const std::array<float, numStemLanes>& stemLaneGains);
#endif

    std::array<std::shared_ptr<const LoadedAudio>, numStemLanes> loadedAudio;
    double currentSampleRate = 44100.0;
    double playheadSamples = 0.0;
    std::atomic<double> publicPlayheadSamples { 0.0 };
    std::atomic<double> pendingSeekSamples    { -1.0 };
    std::atomic<double> loopInRefSamples      { -1.0 };
    std::atomic<double> loopOutRefSamples     { -1.0 };
    std::atomic<bool>   loopActiveFlag        { false };
    int channels = 2;
    int preparedMaxBlockSize = 512;
    EQProcessor eq;
    FilterProcessor filter;

#if SATTARI_STEMDECK_HAS_RUBBERBAND
    std::unique_ptr<RubberBand::RubberBandStretcher> rubberBand;
    juce::AudioBuffer<float> rubberBandInput;
    juce::AudioBuffer<float> rubberBandOutput;
    std::vector<const float*> rubberBandInputPointers;
    std::vector<float*> rubberBandOutputPointers;
    double rubberBandSourceReadSamples = 0.0;
    double rubberBandLastTempoRatio = -1.0;
    double rubberBandLastPitchRatio = -1.0;
    bool rubberBandNeedsReset = true;
#endif
};
