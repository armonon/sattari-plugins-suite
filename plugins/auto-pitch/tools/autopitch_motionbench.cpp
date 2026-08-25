// autopitch_motionbench.cpp — deterministic velocity-estimator benchmark
//
// Phase 3 of the Auto Pitch Intent Engine v3 plan.
// Generates synthetic pitch trajectories with known ground-truth velocity,
// feeds them through ContinuousMotionEstimator, and reports accuracy metrics.
//
// Build: standalone C++17, no JUCE.  See CMakeLists.txt AutoPitchMotionBench.
//
// Usage:
//   autopitch_motionbench [--window-ms W] [--estimator E]
//     --window-ms  20|40|60|80|120   (default: test all)
//     --estimator  fd|mh|ls|med|cw   (default: test all)
//       fd=FirstDerivative  mh=MultiHopAverage  ls=LeastSquares
//       med=RobustMedian    cw=ConfidenceWeighted
//
// Exit 0 if best RMSE ≤ 2.0 MIDI/sec, exit 1 otherwise.
// Prints a summary table and a GO/HOLD verdict.

#include "../src/ContinuousMotionEstimator.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Synthetic trajectory generator
// ---------------------------------------------------------------------------

struct Frame
{
    double freqHz;
    double trueVelMidi;   // ground truth MIDI/sec at this frame
    double trueMidi;
};

static constexpr double kSampleRate = 44100.0;
static constexpr int    kBlockSize  = 256;
static const double     kBlockSec   = (double) kBlockSize / kSampleRate;   // ~5.805 ms

static double midiToHz (double midi)
{
    return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
}

// Linear slide from startMidi → endMidi over durationSec (pre/post: stable)
static std::vector<Frame> makeSlide (double startMidi, double endMidi,
                                     double durationSec,
                                     double preSec  = 0.100,
                                     double postSec = 0.100)
{
    std::vector<Frame> frames;
    const double totalSec = preSec + durationSec + postSec;
    const double trueVel  = (endMidi - startMidi) / durationSec;
    for (double t = 0.0; t < totalSec; t += kBlockSec)
    {
        double midi, vel;
        if (t < preSec)
        {
            midi = startMidi; vel = 0.0;
        }
        else if (t < preSec + durationSec)
        {
            const double st = t - preSec;
            midi = startMidi + (endMidi - startMidi) * st / durationSec;
            vel  = trueVel;
        }
        else
        {
            midi = endMidi; vel = 0.0;
        }
        frames.push_back ({ midiToHz (midi), vel, midi });
    }
    return frames;
}

// Scoop/fall: pitch starts at centerMidi + offsetMidi, decays back to center
static std::vector<Frame> makeScoop (double centerMidi, double offsetMidi,
                                     double durationSec,
                                     double preSec  = 0.100,
                                     double postSec = 0.150)
{
    std::vector<Frame> frames;
    const double totalSec = preSec + durationSec + postSec;
    const double tau      = durationSec / 3.0;
    for (double t = 0.0; t < totalSec; t += kBlockSec)
    {
        double midi, vel;
        if (t < preSec)
        {
            midi = centerMidi + offsetMidi; vel = 0.0;
        }
        else if (t < preSec + durationSec)
        {
            const double st = t - preSec;
            const double e  = std::exp (-st / tau);
            midi = centerMidi + offsetMidi * e;
            vel  = -offsetMidi / tau * e;
        }
        else
        {
            midi = centerMidi; vel = 0.0;
        }
        frames.push_back ({ midiToHz (midi), vel, midi });
    }
    return frames;
}

// Vibrato: sinusoidal modulation
static std::vector<Frame> makeVibrato (double centerMidi, double depthMidi,
                                       double rateHz, double durationSec,
                                       double preSec = 0.100)
{
    std::vector<Frame> frames;
    const double totalSec = preSec + durationSec;
    constexpr double kPi2 = 6.283185307179586;
    for (double t = 0.0; t < totalSec; t += kBlockSec)
    {
        double midi, vel;
        if (t < preSec)
        {
            midi = centerMidi; vel = 0.0;
        }
        else
        {
            const double st = t - preSec;
            midi = centerMidi + depthMidi * std::sin (kPi2 * rateHz * st);
            vel  = depthMidi * kPi2 * rateHz * std::cos (kPi2 * rateHz * st);
        }
        frames.push_back ({ midiToHz (midi), vel, midi });
    }
    return frames;
}

// Stable: constant pitch
static std::vector<Frame> makeStable (double midi, double durationSec)
{
    std::vector<Frame> frames;
    for (double t = 0.0; t < durationSec; t += kBlockSec)
        frames.push_back ({ midiToHz (midi), 0.0, midi });
    return frames;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

struct Metrics
{
    double rmse          = 0.0;   // velocity RMSE (MIDI/sec)
    double dirAcc        = 0.0;   // direction accuracy (fraction, |trueVel|>0.5)
    double peakOvershoot = 0.0;   // max |est - true| (MIDI/sec)
    double falseMovement = 0.0;   // mean |est| on truly stable frames
    double vibratoFP     = 0.0;   // false isVibrato rate on slides
    int    n             = 0;
};

struct RunResult
{
    double windowSec;
    ContinuousMotionEstimator::Estimator est;
    Metrics slides;
    Metrics scoops;
    Metrics vibrato;
    Metrics stable;
    double  overallRmse;
};

static Metrics evalTrajectory (const std::vector<Frame>& traj,
                               double windowSec,
                               ContinuousMotionEstimator::Estimator est,
                               bool expectVibrato = false)
{
    Metrics m;
    ContinuousMotionEstimator cme;
    double sumSqErr = 0.0;
    int    dirN = 0;
    int    dirCorrect = 0;
    int    stableN = 0;
    double stableAbsSum = 0.0;
    int    fpVibrato = 0, fpTotal = 0;

    for (auto& f : traj)
    {
        cme.push (f.freqHz, kBlockSec);
        const auto mf = cme.analyse (windowSec, est);

        const double err  = mf.velocityMidiPerSec - f.trueVelMidi;
        sumSqErr += err * err;
        if (std::abs (err) > m.peakOvershoot) m.peakOvershoot = std::abs (err);
        ++m.n;

        // Direction accuracy: only count when true velocity is clearly directional
        if (std::abs (f.trueVelMidi) > 0.5)
        {
            const double trueDir = f.trueVelMidi > 0 ? 1.0 : -1.0;
            const double estDir  = mf.velocityMidiPerSec > 0.3 ? 1.0
                                 : mf.velocityMidiPerSec < -0.3 ? -1.0 : 0.0;
            ++dirN;
            if (trueDir == estDir) ++dirCorrect;
        }

        // False movement on stable frames (trueVel == 0.0)
        if (f.trueVelMidi == 0.0)
        {
            stableAbsSum += std::abs (mf.velocityMidiPerSec);
            ++stableN;
        }

        // Vibrato false positive: isVibrato reported on a non-vibrato trajectory
        if (! expectVibrato)
        {
            ++fpTotal;
            if (mf.isVibrato) ++fpVibrato;
        }
    }

    m.rmse          = m.n > 0 ? std::sqrt (sumSqErr / (double) m.n) : 0.0;
    m.dirAcc        = dirN  > 0 ? (double) dirCorrect / (double) dirN : 1.0;
    m.falseMovement = stableN > 0 ? stableAbsSum / (double) stableN : 0.0;
    m.vibratoFP     = fpTotal > 0 ? (double) fpVibrato / (double) fpTotal : 0.0;
    return m;
}

static RunResult runOnce (double windowSec,
                          ContinuousMotionEstimator::Estimator est)
{
    RunResult r;
    r.windowSec = windowSec;
    r.est       = est;

    // ---- Slides (semitone, whole-tone, minor third, fourth, octave) ----
    Metrics slideAcc;
    auto addSlide = [&] (double sm, double em, double dur)
    {
        auto traj = makeSlide (sm, em, dur);
        auto m    = evalTrajectory (traj, windowSec, est);
        slideAcc.rmse          += m.rmse;
        slideAcc.dirAcc        += m.dirAcc;
        slideAcc.peakOvershoot  = std::max (slideAcc.peakOvershoot, m.peakOvershoot);
        slideAcc.vibratoFP     += m.vibratoFP;
        slideAcc.n             += m.n;
    };

    const double ctr = 60.0; // C4
    for (double dur : { 0.050, 0.100, 0.200, 0.350, 0.500 })
    {
        addSlide (ctr,         ctr + 1.0,  dur);  // semitone up
        addSlide (ctr,         ctr - 1.0,  dur);  // semitone down
        addSlide (ctr,         ctr + 2.0,  dur);  // whole tone
        addSlide (ctr,         ctr + 3.0,  dur);  // minor third
        addSlide (ctr,         ctr + 5.0,  dur);  // fourth
        addSlide (ctr,         ctr + 12.0, dur);  // octave
    }
    const int slideCount = 5 * 6;   // durations × intervals
    slideAcc.rmse    /= slideCount;
    slideAcc.dirAcc  /= slideCount;
    slideAcc.vibratoFP /= slideCount;
    r.slides = slideAcc;

    // ---- Scoops / falls (±25c, ±50c, ±100c, ±200c) ----
    Metrics scoopAcc;
    auto addScoop = [&] (double offsetCents, double dur)
    {
        auto traj = makeScoop (ctr, offsetCents / 100.0, dur);
        auto m    = evalTrajectory (traj, windowSec, est);
        scoopAcc.rmse    += m.rmse;
        scoopAcc.dirAcc  += m.dirAcc;
        scoopAcc.peakOvershoot = std::max (scoopAcc.peakOvershoot, m.peakOvershoot);
        scoopAcc.n += m.n;
    };
    for (double cents : { 25.0, -25.0, 50.0, -50.0, 100.0, -100.0, 200.0, -200.0 })
        addScoop (cents, 0.150);
    scoopAcc.rmse   /= 8.0;
    scoopAcc.dirAcc /= 8.0;
    r.scoops = scoopAcc;

    // ---- Vibrato (4–8 Hz, ±10–70c) ----
    Metrics vibAcc;
    auto addVib = [&] (double rateHz, double depthCents)
    {
        auto traj = makeVibrato (ctr, depthCents / 100.0, rateHz, 0.500);
        auto m    = evalTrajectory (traj, windowSec, est, /*expectVibrato=*/true);
        vibAcc.rmse   += m.rmse;
        vibAcc.dirAcc += m.dirAcc;
        vibAcc.n      += m.n;
    };
    int vibCount = 0;
    for (double rate : { 4.0, 5.0, 6.0, 8.0 })
        for (double depth : { 10.0, 30.0, 50.0, 70.0 })
            { addVib (rate, depth); ++vibCount; }
    vibAcc.rmse   /= vibCount;
    vibAcc.dirAcc /= vibCount;
    r.vibrato = vibAcc;

    // ---- Stable notes (ensure velocity → 0) ----
    {
        Metrics stAcc;
        for (double midi : { 48.0, 57.0, 60.0, 69.0, 76.0 })
        {
            auto traj = makeStable (midi, 0.300);
            auto m    = evalTrajectory (traj, windowSec, est);
            stAcc.falseMovement += m.falseMovement;
            stAcc.n             += m.n;
        }
        stAcc.falseMovement /= 5.0;
        r.stable = stAcc;
    }

    r.overallRmse = (r.slides.rmse + r.scoops.rmse + r.vibrato.rmse) / 3.0;
    return r;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* estName (ContinuousMotionEstimator::Estimator e)
{
    switch (e)
    {
        case ContinuousMotionEstimator::Estimator::FirstDerivative:    return "fd ";
        case ContinuousMotionEstimator::Estimator::MultiHopAverage:    return "mh ";
        case ContinuousMotionEstimator::Estimator::LeastSquares:       return "ls ";
        case ContinuousMotionEstimator::Estimator::RobustMedian:       return "med";
        case ContinuousMotionEstimator::Estimator::ConfidenceWeighted: return "cw ";
    }
    return "???";
}

static ContinuousMotionEstimator::Estimator parseEst (const char* s)
{
    if (std::strcmp (s, "fd")  == 0) return ContinuousMotionEstimator::Estimator::FirstDerivative;
    if (std::strcmp (s, "mh")  == 0) return ContinuousMotionEstimator::Estimator::MultiHopAverage;
    if (std::strcmp (s, "ls")  == 0) return ContinuousMotionEstimator::Estimator::LeastSquares;
    if (std::strcmp (s, "med") == 0) return ContinuousMotionEstimator::Estimator::RobustMedian;
    if (std::strcmp (s, "cw")  == 0) return ContinuousMotionEstimator::Estimator::ConfidenceWeighted;
    std::fprintf (stderr, "Unknown estimator '%s'. Valid: fd mh ls med cw\n", s);
    std::exit (1);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main (int argc, char** argv)
{
    // Parse optional filters
    std::vector<double> windows = { 0.020, 0.040, 0.060, 0.080, 0.120 };
    std::vector<ContinuousMotionEstimator::Estimator> estimators = {
        ContinuousMotionEstimator::Estimator::FirstDerivative,
        ContinuousMotionEstimator::Estimator::MultiHopAverage,
        ContinuousMotionEstimator::Estimator::LeastSquares,
        ContinuousMotionEstimator::Estimator::RobustMedian,
        ContinuousMotionEstimator::Estimator::ConfidenceWeighted,
    };

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--window-ms") == 0 && i + 1 < argc)
        {
            windows = { std::stod (argv[++i]) / 1000.0 };
        }
        else if (std::strcmp (argv[i], "--estimator") == 0 && i + 1 < argc)
        {
            estimators = { parseEst (argv[++i]) };
        }
        else if (std::strcmp (argv[i], "--help") == 0 || std::strcmp (argv[i], "-h") == 0)
        {
            std::puts (
                "usage: autopitch_motionbench [--window-ms W] [--estimator E]\n"
                "  --window-ms  20|40|60|80|120   (default: test all)\n"
                "  --estimator  fd|mh|ls|med|cw   (default: test all)\n"
                "    fd=FirstDerivative  mh=MultiHopAverage  ls=LeastSquares\n"
                "    med=RobustMedian    cw=ConfidenceWeighted\n"
                "Exit 0 if best overall RMSE <= 2.0 MIDI/sec, else 1.\n");
            return 0;
        }
    }

    // Header
    std::puts ("autopitch_motionbench — Phase 3: Continuous Motion Estimator\n");
    std::printf ("%-7s  %-5s  %9s  %9s  %9s  %9s  %9s  %9s\n",
                 "win-ms", "est",
                 "sl-RMSE", "sc-RMSE", "vib-RMSE", "ALL-RMSE",
                 "sl-dir%", "stab-mv");
    std::puts ("-------  -----  ---------  ---------  ---------  ---------  ---------  ---------");

    double bestRmse = 1e9;
    RunResult bestResult {};

    for (double win : windows)
    {
        for (auto est : estimators)
        {
            const auto r = runOnce (win, est);

            std::printf ("%-7.0f  %-5s  %9.3f  %9.3f  %9.3f  %9.3f  %9.1f  %9.3f\n",
                         win * 1000.0,
                         estName (est),
                         r.slides.rmse,
                         r.scoops.rmse,
                         r.vibrato.rmse,
                         r.overallRmse,
                         r.slides.dirAcc * 100.0,
                         r.stable.falseMovement);

            if (r.overallRmse < bestRmse)
            {
                bestRmse   = r.overallRmse;
                bestResult = r;
            }
        }
        std::puts ("");
    }

    // Detail report for best combo
    std::puts ("\n=== Best configuration ===");
    std::printf ("  window:     %.0f ms\n",  bestResult.windowSec * 1000.0);
    std::printf ("  estimator:  %s\n",        estName (bestResult.est));
    std::printf ("  slide RMSE: %.3f MIDI/sec\n",  bestResult.slides.rmse);
    std::printf ("  scoop RMSE: %.3f MIDI/sec\n",  bestResult.scoops.rmse);
    std::printf ("  vibr  RMSE: %.3f MIDI/sec\n",  bestResult.vibrato.rmse);
    std::printf ("  overall:    %.3f MIDI/sec\n",  bestResult.overallRmse);
    std::printf ("  slide dir:  %.1f%%\n",    bestResult.slides.dirAcc * 100.0);
    std::printf ("  scoop dir:  %.1f%%\n",    bestResult.scoops.dirAcc * 100.0);
    std::printf ("  stab false: %.3f MIDI/sec\n",  bestResult.stable.falseMovement);
    std::printf ("  vib FP:     %.1f%%\n",    bestResult.slides.vibratoFP * 100.0);
    std::puts ("");

    constexpr double kTargetRmse = 2.0;

    if (bestRmse <= kTargetRmse)
    {
        std::printf ("GO — MOTION SIGNAL SUFFICIENT  (%.3f ≤ %.1f MIDI/sec)\n",
                     bestRmse, kTargetRmse);
        std::puts ("    → Set IntentV2State::kSlideDestinationV2Enabled = true");
        std::puts ("    → Update ContinuousMotionEstimator::analyse() defaults");
        std::puts ("      to the winning window/estimator above, then re-run");
        std::puts ("      IntentBench (Phase 5).");
        return 0;
    }
    else
    {
        std::printf ("HOLD — MOTION SIGNAL INSUFFICIENT  (%.3f > %.1f MIDI/sec)\n",
                     bestRmse, kTargetRmse);
        std::puts ("    → Phase 4 (SLIDE_DESTINATION_V2) must stay disabled.");
        std::puts ("    → Investigate YIN jitter, consider wider windows or");
        std::puts ("      a sub-sample frequency smoothing pre-pass.");
        return 1;
    }
}
