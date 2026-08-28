#pragma once

// =============================================================================
//  SattariStepClock — the suite's shared step clock.
// -----------------------------------------------------------------------------
//  Turns a transport into TIMED STEP TICKS, and nothing else. It does not know
//  what a step does: fire a drum, emit a note, move a filter cutoff. That
//  ignorance is the entire point.
//
//  The suite already owned a 16x64 probabilistic grid (SattariBeatGrid) and a
//  generative engine over it (SattariGroove) — but the clock that drove them was
//  fused into Rhythm Genie's drum renderer, so exactly one plugin could sequence
//  anything. Pulling the clock out is what lets the same grid drive notes in an
//  instrument and parameters in an effect rack.
//
//  WHAT IT GETS RIGHT, because these are the parts that are easy to get wrong:
//
//   * HOST PPQ IS THE TRUTH WHEN IT EXISTS. Deriving step positions from a
//     free-running sample counter drifts against the host within seconds. When
//     no usable PPQ arrives — standalone, or a host that reports none — it
//     free-runs its own, so the thing still plays instead of sitting silent.
//
//   * STEPS ARE PLACED IN PPQ, NOT IN SAMPLES. Computing "samples per step" and
//     accumulating loses a fraction every block, and the error is cumulative.
//     Each step's onset is computed absolutely from its index, so step 10,000 is
//     as accurate as step 1.
//
//   * THE SCAN STARTS ONE STEP EARLY. A swung odd step is DELAYED, so a step
//     whose nominal position is before this block can still land inside it.
//     Starting the scan at the current step would silently drop it.
//
//  PURE STD, deliberately. It takes its own tiny transport POD rather than
//  sattari::host::TransportSnapshot, because that header pulls in JUCE and would
//  make this untestable without an audio host — the same reason BeatGrid and
//  Groove are pure. Callers fill StepTransport from the snapshot in one line.
//  No allocation, no locks: safe on the audio thread.
// =============================================================================

#include <cmath>

namespace sattari::dsp
{

struct StepTransport
{
    double bpm       = 120.0;
    double ppq       = 0.0;    // host timeline position in quarter notes
    bool   hasPpq    = false;  // false => free-run our own clock
    bool   isPlaying = false;
};

struct StepTick
{
    int step         = 0;   // index within the cycle, always in [0, stepsPerCycle)
    int sampleOffset = 0;   // where in this block the step lands
    long long absolute = 0; // monotonic step counter; survives cycle wrap
};

class StepClock
{
public:
    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        internalPpq_ = 0.0;
        currentStep_ = -1;
        running_ = false;
    }

    //  Advance by one block and report every step landing inside it.
    //
    //  stepsPerBeat: 4 = sixteenths, 2 = eighths, 8 = thirty-seconds, 3 = eighth
    //  triplets. stepsPerCycle is the loop length (16 = one bar of sixteenths).
    //  swing delays every ODD step by up to half a step, which is the usual
    //  meaning of the control and matches Rhythm Genie's existing behaviour.
    int process (const StepTransport& transport,
                 int numSamples, int stepsPerBeat, int stepsPerCycle,
                 float swing, StepTick* out, int maxOut) noexcept
    {
        if (numSamples <= 0 || out == nullptr || maxOut <= 0) return 0;
        if (stepsPerBeat < 1) stepsPerBeat = 1;
        if (stepsPerCycle < 1) stepsPerCycle = 1;

        bpm_ = transport.bpm > 0.0 ? transport.bpm : 120.0;
        const double samplesPerBeat = sampleRate_ * 60.0 / bpm_;
        const double blockBeats     = (double) numSamples / samplesPerBeat;
        const double stepBeats      = 1.0 / (double) stepsPerBeat;

        // No usable PPQ means no host timeline to follow, so free-run. The honest
        // test is whether a PPQ actually arrived, not which wrapper we are.
        const bool freeRun = ! transport.hasPpq;
        const double startPpq = freeRun ? internalPpq_ : transport.ppq;
        running_ = transport.isPlaying || freeRun;
        if (freeRun) internalPpq_ += blockBeats;

        if (! running_) { currentStep_ = -1; return 0; }

        const double endPpq   = startPpq + blockBeats;
        const double swingPpq = (double) swing * stepBeats * 0.5;

        int count = 0;
        // Start one step early: a swung odd step is delayed, so its nominal
        // position can sit before this block while its actual onset is inside it.
        for (long long k = (long long) std::floor (startPpq / stepBeats) - 1; ; ++k)
        {
            const double onset = (double) k * stepBeats + ((k & 1) ? swingPpq : 0.0);
            if (onset >= endPpq) break;
            if (onset < startPpq) continue;
            if (count >= maxOut) break;

            int offset = (int) std::lround ((onset - startPpq) * samplesPerBeat);
            if (offset < 0) offset = 0;
            if (offset > numSamples - 1) offset = numSamples - 1;

            const long long wrapped = ((k % stepsPerCycle) + stepsPerCycle) % stepsPerCycle;
            out[count++] = StepTick { (int) wrapped, offset, k };
            currentStep_ = (int) wrapped;
        }
        return count;
    }

    bool   isRunning()   const noexcept { return running_; }
    int    currentStep() const noexcept { return currentStep_; }   // -1 when stopped
    double bpm()         const noexcept { return bpm_; }

private:
    double sampleRate_  = 48000.0;
    double internalPpq_ = 0.0;
    double bpm_         = 120.0;
    int    currentStep_ = -1;
    bool   running_     = false;
};

} // namespace sattari::dsp
