#pragma once

#include <algorithm>
#include <cmath>

namespace sattari::dsp
{

struct MonophonicPitchEstimate
{
    double frequencyHz = 0.0;
    double confidence = 0.0;
    bool voiced = false;
};

// Conservative realtime fusion for the deterministic YIN and neural CREPE
// trackers. Neural information validates an agreeing YIN estimate and rescues a
// high-confidence frame when YIN is unvoiced; it never overrules a confident,
// conflicting YIN pitch. That asymmetry is intentional: an isolated neural
// octave/bin error must not become an audible full-note correction error. The
// agreeing path intentionally keeps YIN's frequency contour instead of averaging
// it with CREPE: CREPE's 64 ms frame plus causal median has substantially more
// group delay, and blending the two contours phase-shifts vibrato before the
// correction engine. Neural confidence can strengthen trust without smearing the
// timing of the pitch curve.
class MonophonicPitchFusion
{
public:
    void reset() noexcept
    {
        lastAcceptedMidi = -1000.0;
        rejectedOrUnvoicedFrames = 0;
    }

    MonophonicPitchEstimate fuse (MonophonicPitchEstimate yin,
                                  MonophonicPitchEstimate neural,
                                  double referenceHz = 440.0) noexcept
    {
        normalise (yin);
        normalise (neural);
        referenceHz = referenceHz > 0.0 ? referenceHz : 440.0;

        if (yin.voiced)
        {
            auto accepted = yin;
            if (neural.voiced && neural.confidence >= 0.50)
            {
                const double yinMidi = midiFromHz (yin.frequencyHz, referenceHz);
                double neuralMidi = midiFromHz (neural.frequencyHz, referenceHz);

                // CREPE octave errors preserve pitch class. Fold its estimate to
                // the octave nearest YIN before testing agreement, then use it only
                // as a validator when the two trackers are within 80 cents.
                while (neuralMidi - yinMidi > 6.0) neuralMidi -= 12.0;
                while (yinMidi - neuralMidi > 6.0) neuralMidi += 12.0;
                if (std::abs (neuralMidi - yinMidi) <= 0.80)
                {
                    const double wy = std::max (0.05, yin.confidence * yin.confidence);
                    const double wn = std::max (0.05, neural.confidence * neural.confidence);
                    accepted.confidence = std::clamp ((wy * yin.confidence + wn * neural.confidence)
                                                       / (wy + wn), 0.0, 1.0);
                }
            }
            accept (accepted, referenceHz);
            return accepted;
        }

        // CREPE can rescue breathy/noisy frames that YIN declines, but only at a
        // stronger confidence floor. A sudden neural-only leap is withheld briefly;
        // after three rejected/unvoiced frames it is treated as a genuine new onset.
        if (neural.voiced && neural.confidence >= 0.70)
        {
            const double midi = midiFromHz (neural.frequencyHz, referenceHz);
            const bool continuous = lastAcceptedMidi < -900.0
                                 || std::abs (midi - lastAcceptedMidi) <= 3.0
                                 || rejectedOrUnvoicedFrames >= 3;
            if (continuous)
            {
                accept (neural, referenceHz);
                return neural;
            }
        }

        ++rejectedOrUnvoicedFrames;
        return {};
    }

private:
    static void normalise (MonophonicPitchEstimate& p) noexcept
    {
        p.confidence = std::clamp (p.confidence, 0.0, 1.0);
        if (! p.voiced || ! std::isfinite (p.frequencyHz) || p.frequencyHz <= 0.0)
        {
            p.frequencyHz = 0.0;
            p.voiced = false;
        }
    }

    static double midiFromHz (double hz, double referenceHz) noexcept
    {
        return 69.0 + 12.0 * std::log2 (hz / referenceHz);
    }

    static double hzFromMidi (double midi, double referenceHz) noexcept
    {
        return referenceHz * std::pow (2.0, (midi - 69.0) / 12.0);
    }

    void accept (const MonophonicPitchEstimate& p, double referenceHz) noexcept
    {
        lastAcceptedMidi = midiFromHz (p.frequencyHz, referenceHz);
        rejectedOrUnvoicedFrames = 0;
    }

    double lastAcceptedMidi = -1000.0;
    int rejectedOrUnvoicedFrames = 0;
};

} // namespace sattari::dsp
