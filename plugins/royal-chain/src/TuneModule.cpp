#include "TuneModule.h"

#include <cmath>

namespace
{
// Auto Pitch Live Mode tracks 70 Hz .. 1.1 kHz with the Extreme profile (the dual
// tracker's 100 Hz fast path handles ordinary vocal notes, the full-range one
// takes over for low baritone/bass notes).
constexpr double kMinHz = 70.0;
constexpr double kMaxHz = 1100.0;
}

void TuneModule::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int block = juce::jmax (1, maxBlockSize);
    for (auto& e : engines)
    {
        e.prepare (sr, block, kMinHz, kMaxHz);   // all allocation happens here
        e.setFormantPreservation (formantPreservation);
    }
    latencySamples = engines[0].getLatencySamples();
    preparedChannels = kMaxChannels;
    reset();
}

void TuneModule::setFormantPreservation (bool shouldPreserve) noexcept
{
    formantPreservation = shouldPreserve;
    for (auto& e : engines)
        e.setFormantPreservation (shouldPreserve);
}

void TuneModule::reset()
{
    for (auto& e : engines)
        e.reset();
    confidence = 0.0f;
    lastHz = 0.0f;
    lastVoiced = false;
    correctionCents = 0.0f;
    targetMidi = -1;
}

// Vocal Style -> Auto Pitch Live parameters. Retune times sit on Auto Pitch's own
// Speed scale (Speed 0.05..1 maps to 120..3 ms; its default Speed 0.85 = 21.5 ms).
// Styles that live on ornament and vibrato (R&B runs, Persian tahrir, Indie) keep
// slower retunes, more vibrato and gentler note transitions; hard, modern styles
// (Rap, Hook) reach Auto Pitch's Classic-like 3 ms snap at full Tune.
TuneModule::StyleProfile TuneModule::profileForStyle (int vocalStyle) noexcept
{
    switch (vocalStyle)
    {
        case 0:  return {  3.0,  40.0, 0.00, 0.90, 1.0 };   // Modern Rap: hard, flat
        case 1:  return { 12.0,  80.0, 0.60, 0.40, 1.0 };   // R&B: keep runs + vibrato
        case 2:  return {  6.0,  60.0, 0.35, 0.60, 1.0 };   // Pop: tight but natural
        case 3:  return { 18.0, 100.0, 0.80, 0.30, 1.0 };   // Persian/World Pop: ornaments
        case 4:  return { 20.0, 110.0, 0.70, 0.35, 1.0 };   // Indie: loose, human
        case 5:  return { 40.0, 120.0, 1.00, 0.20, 0.5 };   // Podcast/Spoken: barely
        case 6:  return {  5.0,  50.0, 0.20, 0.70, 1.0 };   // Background: locked stacks
        case 7:  return {  3.0,  45.0, 0.10, 0.85, 1.0 };   // Hook: hard, glossy
        case 8:  return {  8.0,  60.0, 0.40, 0.60, 1.0 };   // Demo Rescue
        case 9:  return { 10.0,  70.0, 0.50, 0.60, 1.0 };   // Live
        default: return {  6.0,  60.0, 0.35, 0.60, 1.0 };
    }
}

sattari::dsp::LiveCorrectionPlanner::Settings
TuneModule::settingsFor (float tuneAmount, int keyRoot, sattari::dsp::ScaleId scale, int vocalStyle) noexcept
{
    sattari::dsp::LiveCorrectionPlanner::Settings s;
    const double tune = juce::jlimit (0.0, 1.0, (double) tuneAmount);
    const auto p = profileForStyle (vocalStyle);

    // Strength reaches full correction at 60 % of the macro; the rest of the throw
    // only speeds the retune toward the style's hard setting (log-interpolated so
    // the knob feels even from slow-natural to snap).
    s.amount = juce::jlimit (0.0, 1.0, tune / 0.6) * p.amountScale;
    s.retuneMilliseconds = p.softRetuneMs * std::pow (p.hardRetuneMs / p.softRetuneMs, tune);
    s.preserveVibrato = p.preserveVibrato;
    s.transitionAmount = p.transition;
    s.noteHysteresisCents = 18.0;   // Auto Pitch Live default
    s.referenceHz = 440.0;
    s.pitchOffsetCents = 0.0;
    s.forcedTargetMidi = -1;

    // Legal pitch classes: same key/scale tables as snapMidiToScale.
    const int root = ((keyRoot % 12) + 12) % 12;
    int count = 0;
    const int* intervals = sattari::dsp::scaleIntervals (scale, count);
    if (intervals == nullptr || count <= 0)
    {
        s.enabledNotes.fill (true);
    }
    else
    {
        s.enabledNotes.fill (false);
        for (int i = 0; i < count; ++i)
            s.enabledNotes[(size_t) ((root + intervals[i]) % 12)] = true;
    }
    return s;
}

void TuneModule::process (juce::AudioBuffer<float>& buffer, float tuneAmount,
                          int keyRoot, sattari::dsp::ScaleId scale, int vocalStyle)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const auto settings = settingsFor (tuneAmount, keyRoot, scale, vocalStyle);

    // The engine is block-size invariant on its own (it latches tracking,
    // planning and the shifter's period/ratio on an internal ~0.67 ms sample
    // clock). The loop below exists only for stereo linkage: channel 0 plans and
    // the other channel follows its exact ratio (Auto Pitch's
    // processWithLiveEngine contract), so a stereo vocal never splits in pitch.
    // Cutting the host block on the lead's own control grid hands the follower
    // the lead's ratio for that exact grid period, which keeps the follower
    // block-size invariant too. Sub-buffers alias the host buffer, so this stays
    // allocation-free; mono runs one call per block.
    auto& lead = engines[0];
    double cents = lead.getCorrectionCents();
    for (int start = 0; start < numSamples;)
    {
        const int n = numChannels > 1 ? juce::jmin (lead.getSamplesUntilControlUpdate(), numSamples - start)
                                      : numSamples - start;
        lead.processCorrected (buffer.getReadPointer (0, start), buffer.getWritePointer (0, start), n, settings);
        cents = lead.getCorrectionCents();
        if (numChannels > 1)
        {
            const double ratio = std::pow (2.0, cents / 1200.0);
            for (int ch = 1; ch < numChannels; ++ch)
                engines[(size_t) ch].process (buffer.getReadPointer (ch, start),
                                              buffer.getWritePointer (ch, start), n, ratio);
        }
        start += n;
    }

    const auto pitch = lead.getPitchResult();
    lastVoiced = pitch.voiced && pitch.frequencyHz > 0.0;
    lastHz = lastVoiced ? (float) pitch.frequencyHz : 0.0f;
    correctionCents = lastVoiced ? (float) cents : 0.0f;
    targetMidi = lastVoiced ? lead.getTargetMidi() : -1;

    // Periodicity confidence, time-normalised (~50 ms) so the meter behaves the
    // same at every buffer size. Silence reads as zero, not the last note's value.
    const float target = pitch.rms > 1.0e-5f ? juce::jlimit (0.0f, 1.0f, pitch.confidence) : 0.0f;
    const float a = 1.0f - (float) std::exp (-(double) numSamples / (0.05 * sr));
    confidence += a * (target - confidence);
}
