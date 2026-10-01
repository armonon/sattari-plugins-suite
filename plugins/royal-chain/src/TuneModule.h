#pragma once

#include <JuceHeader.h>
#include <sattari_audio_core/SattariLivePitchEngine.h>
#include <sattari_audio_core/SattariPitchDetector.h>
#include <array>

// Real-time monophonic pitch correction for the Royal Chain Tune stage.
//
// This runs the SAME correction engine as Auto Pitch's Live Mode with its default
// "Extreme Live (2 ms)" profile: sattari::dsp::ExtremeLivePitchEngine from
// sattari_audio_core (dual causal YIN trackers with voiced hysteresis, the
// LiveCorrectionPlanner's note/vibrato decomposition with retune + transition
// time constants, and the fixed-2 ms pitch-synchronous shifter). Channel 0 plans
// the correction; further channels follow its exact ratio, as in Auto Pitch.
//
// Latency is a constant engine property (2 ms, e.g. 96 samples at 48 kHz),
// independent of the Tune amount and of formant preservation, so the host's
// delay compensation never moves while the macro is automated. Tune 0 returns
// the exact delayed input.
//
// Formant preservation is ON by default here: the engine's causal LPC
// correction keeps the singer's vowel/formant envelope while the pitch moves
// (the bare Extreme shifter resamples it with the pitch, e.g. +6 % per
// semitone). It adds no latency. Auto Pitch keeps the engine default (off).
class TuneModule
{
public:
    TuneModule() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    // tuneAmount 0..1 (the Tune macro). keyRoot 0=C..11=B. ScaleId::ChromaticOff
    // snaps to all 12 semitones. vocalStyle is the Royal Chain "style" choice index
    // (0=Modern Rap .. 9=Live) and picks the retune character.
    void process (juce::AudioBuffer<float>& buffer, float tuneAmount,
                  int keyRoot = 0,
                  sattari::dsp::ScaleId scale = sattari::dsp::ScaleId::ChromaticOff,
                  int vocalStyle = 2);

    // Constant for a prepared sample rate; safe to call from prepareToPlay.
    int getLatencySamples() const noexcept { return latencySamples; }

    // Default true. Applied at prepare(); changing it while running takes
    // effect immediately but restarts the engines' cycle buffers (call it from
    // a non-realtime context or between phrases).
    void setFormantPreservation (bool shouldPreserve) noexcept;
    bool getFormantPreservation() const noexcept { return formantPreservation; }

    float getConfidence() const noexcept { return confidence; }
    // Last detected (sung, pre-correction) fundamental in Hz when the engine's
    // tracker calls it voiced, else 0. Read on the audio thread right after
    // process(); the processor republishes it through a relaxed atomic for the
    // editor's Compass.
    float getLastHz() const noexcept { return lastVoiced ? lastHz : 0.0f; }
    // Correction currently applied by the planner (cents) and its target note.
    float getCorrectionCents() const noexcept { return correctionCents; }
    int getTargetMidi() const noexcept { return targetMidi; }

    // Style -> Auto Pitch Live parameter mapping (exposed for tests/docs).
    struct StyleProfile
    {
        double hardRetuneMs;      // retune time constant at Tune = 1
        double softRetuneMs;      // retune time constant as Tune -> 0
        double preserveVibrato;   // Auto Pitch "Preserve Vibrato" (0..1)
        double transition;        // Auto Pitch "Transition" (0..1)
        double amountScale;       // ceiling on correction strength
    };
    static StyleProfile profileForStyle (int vocalStyle) noexcept;
    static sattari::dsp::LiveCorrectionPlanner::Settings
        settingsFor (float tuneAmount, int keyRoot, sattari::dsp::ScaleId scale, int vocalStyle) noexcept;

private:
    static constexpr int kMaxChannels = 2;

    double sr = 44100.0;
    int latencySamples = 0;
    int preparedChannels = 0;
    bool formantPreservation = true;
    float confidence = 0.0f;
    float lastHz = 0.0f;
    bool lastVoiced = false;
    float correctionCents = 0.0f;
    int targetMidi = -1;

    std::array<sattari::dsp::ExtremeLivePitchEngine, kMaxChannels> engines;
};
