#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <memory>
#include <vector>

namespace RubberBand { class RubberBandStretcher; }

// Real-time monophonic pitch correction for the Royal Chain Tune stage. It shares
// the suite's Rubber Band realtime engine (the same one Auto Pitch uses) and the
// shared sattari::dsp::PitchDetector: detect the sung pitch, snap to the nearest
// equal-tempered note, and pitch-shift toward it by the Tune amount. When Rubber
// Band is not compiled in, it passes audio through unchanged (still reporting
// input confidence) so the chain always builds.
class TuneModule
{
public:
    TuneModule();
    ~TuneModule();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, float tuneAmount);
    float getConfidence() const { return confidence; }

private:
    double sr = 44100.0;
    int maxBlock = 512;
    float confidence = 0.0f;

    sattari::dsp::PitchDetector pitchDetector;   // shared suite detector
    std::vector<float> monoScratch;
    int samplesSinceAnalysis = 0;

    double smoothedScale = 1.0;   // glided correction ratio

#if SATTARI_ROYALCHAIN_HAS_RUBBERBAND
    std::vector<std::unique_ptr<RubberBand::RubberBandStretcher>> stretchers;
    juce::AudioBuffer<float> rbOut;
#endif
};
