#pragma once

#include <sattari_audio_core/SattariModule.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  WarpModule — Sattari Warp's DSP, extracted so the standalone plugin and a
//  Create rack slot run the SAME transformer.
//
//  A destructive-in-a-good-way vocal FX unit. One DOSE macro escalates a chain
//  of real DSP blocks from "subtle character" to "fully mangled":
//
//    1. VOWEL   — morphable formant filter (two bandpass resonators sweeping
//                 A-E-I-O-U formant pairs);
//    2. ROBOT   — ring modulation against a tuned carrier;
//    3. GRIT    — waveshaper drive, then bit- and sample-rate crush;
//    4. STUTTER — tempo-locked beat-repeat.
//
//  DOSE multiplies every stage's amount, so a single knob walks the whole chain
//  and every stage still has its own trim. At dose = 0 each stage's amount falls
//  under its epsilon and is skipped entirely, so the module is a pass-through.
//
//  STUTTER runs AFTER the per-sample chain rather than inside it, so every
//  channel shares one slice timing — inside the loop the channels would drift
//  into separate slices and the repeat would smear across the stereo image.
// =============================================================================

namespace sattari::dsp
{

class WarpModule final : public SattariModule
{
public:
    static juce::StringArray stutterRateChoices() { return { "1/4", "1/8", "1/8T", "1/16", "1/32" }; }

    const char* moduleId()    const noexcept override { return "warp"; }
    const char* displayName() const noexcept override { return "Warp"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FloatP  = juce::AudioParameterFloat;
        using ChoiceP = juce::AudioParameterChoice;
        using R       = juce::NormalisableRange<float>;

        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "input",  1 }, "Input",  R (-24.0f, 12.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "output", 1 }, "Output", R (-24.0f, 12.0f, 0.1f), 0.0f));
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "mix",    1 }, "Mix",    R (0.0f, 1.0f, 0.001f), 1.0f));

        // DOSE — the master intensity macro. Scales every block below.
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "dose",   1 }, "Dose",   R (0.0f, 1.0f, 0.001f), 0.5f));

        // VOWEL
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "vowel",    1 }, "Vowel",        R (0.0f, 1.0f, 0.001f), 0.4f));
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "vowelAmt", 1 }, "Vowel Amount", R (0.0f, 1.0f, 0.001f), 0.45f));

        // ROBOT (ring mod)
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "robotFreq", 1 }, "Robot Freq", R (20.0f, 1200.0f, 0.1f, 0.35f), 120.0f));
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "robotAmt",  1 }, "Robot",      R (0.0f, 1.0f, 0.001f), 0.25f));

        // GRIT (drive + crush)
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "drive", 1 }, "Drive", R (0.0f, 1.0f, 0.001f), 0.35f));
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "crush", 1 }, "Crush", R (0.0f, 1.0f, 0.001f), 0.2f));

        // STUTTER (beat-repeat)
        out.push_back (std::make_unique<FloatP>  (juce::ParameterID { prefix + "stutter",     1 }, "Stutter",      R (0.0f, 1.0f, 0.001f), 0.0f));
        out.push_back (std::make_unique<ChoiceP> (juce::ParameterID { prefix + "stutterRate", 1 }, "Stutter Rate", stutterRateChoices(), 3));
    }

    void link (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Sized here, never in process(): the shell used to setSize() per block, which
        // allocates the first time the block grows.
        dryScratch.setSize (juce::jmax (2, numChannels), juce::jmax (1, maxBlockSize));

        // Stutter buffer: up to a 1/4 note at 40 BPM (worst case) — plenty of slack.
        stutterCap = (int) (sr * 1.6);
        for (auto& v : stutterBuf) v.assign ((size_t) stutterCap, 0.0f);

        reset();
    }

    void reset() override
    {
        for (auto& b : vowelF1) b.reset();
        for (auto& b : vowelF2) b.reset();
        ringPhase  = { { 0.0, 0.0 } };
        crushHold  = { { 0.0f, 0.0f } };
        crushCount = { { 0, 0 } };
        for (auto& v : stutterBuf) std::fill (v.begin(), v.end(), 0.0f);
        stutterWrite = stutterPlay = stutterPos = 0;
        stutterLen = juce::jlimit (1, juce::jmax (1, stutterCap), (int) (sr * 0.125));
        dryScratch.clear();
        outLevel.store (0.0f, std::memory_order_relaxed);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override
    {
        if (t.hasHostBpm && t.bpm > 20.0) hostBpm = t.bpm;
    }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;
        if (dryScratch.getNumSamples() < n || dryScratch.getNumChannels() < numCh) return;

        const float inGain  = juce::Decibels::decibelsToGain (p ("input"));
        const float outGain = juce::Decibels::decibelsToGain (p ("output"));
        const float mix     = p ("mix");
        const float dose    = p ("dose");

        // Apply input gain, then stash the dry signal for the dry/wet blend.
        buffer.applyGain (inGain);
        for (int ch = 0; ch < numCh; ++ch)
            dryScratch.copyFrom (ch, 0, buffer, ch, 0, n);

        // Effective (dose-scaled) block amounts.
        double f1 = 730.0, f2 = 1090.0;
        vowelFormants (p ("vowel"), f1, f2);
        const float vowelAmt = p ("vowelAmt") * dose;
        const float robotAmt = p ("robotAmt") * dose;
        const double robotInc = 2.0 * juce::MathConstants<double>::pi * (double) p ("robotFreq") / sr;
        const float drive     = p ("drive") * dose;
        const float driveK    = 1.0f + drive * 24.0f;
        const float crush     = p ("crush") * dose;
        const int   crushBits = (int) std::round (juce::jmap (crush, 0.0f, 1.0f, 16.0f, 3.0f));   // 16 -> 3 bits
        const int   crushDown = 1 + (int) std::round (crush * 12.0f);                             // 1..13x downsample
        const float crushLevels = (float) (1 << juce::jlimit (2, 16, crushBits));
        const float stutterAmt  = p ("stutter") * dose;

        // Stutter slice length from tempo (falls back to 120 BPM).
        const int rateIdx = juce::jlimit (0, 4, (int) std::round (p ("stutterRate")));
        static constexpr double stutterRateBeats[5] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };
        const double beatSec = 60.0 / juce::jmax (20.0, hostBpm);
        stutterLen = juce::jlimit (32, juce::jmax (32, stutterCap),
                                   (int) (beatSec * stutterRateBeats[rateIdx] * sr));

        // Configure the vowel resonators for this block.
        for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
        {
            vowelF1[(size_t) ch].setBandpass (sr, f1, 6.0);
            vowelF2[(size_t) ch].setBandpass (sr, f2, 9.0);
        }

        float peak = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const int c = juce::jmin (ch, 1);   // reuse channel-0 state for >2 ch
            float* d = buffer.getWritePointer (ch);
            double rp = ringPhase[(size_t) c];

            for (int i = 0; i < n; ++i)
            {
                float x = d[i];   // input gain already applied buffer-wide

                // 1) VOWEL — blend in the formant-filtered signal.
                if (vowelAmt > 1.0e-4f)
                {
                    const float formant = 0.6f * vowelF1[(size_t) c].process (x)
                                        + 0.5f * vowelF2[(size_t) c].process (x);
                    x = x * (1.0f - vowelAmt) + formant * vowelAmt * 1.8f;
                }

                // 2) ROBOT — ring modulation.
                if (robotAmt > 1.0e-4f)
                {
                    const float carrier = (float) std::sin (rp);
                    x = x * (1.0f - robotAmt) + (x * carrier) * robotAmt;
                }
                rp += robotInc;
                if (rp > 2.0 * juce::MathConstants<double>::pi) rp -= 2.0 * juce::MathConstants<double>::pi;

                // 3) GRIT — waveshaper drive then bit / sample-rate crush.
                if (drive > 1.0e-4f)
                    x = std::tanh (x * driveK) * (1.0f / std::tanh (driveK)) * (0.6f + 0.4f * drive) + x * (1.0f - drive);
                if (crush > 1.0e-4f)
                {
                    if (crushCount[(size_t) c] <= 0)
                    {
                        crushHold[(size_t) c] = std::round (x * crushLevels) / crushLevels;
                        crushCount[(size_t) c] = crushDown;
                    }
                    --crushCount[(size_t) c];
                    x = x * (1.0f - crush) + crushHold[(size_t) c] * crush;
                }

                d[i] = x;   // write wet-so-far; STUTTER captures/replays below per channel
            }

            ringPhase[(size_t) c] = rp;
        }

        // 4) STUTTER — capture a slice and loop it, blended by amount. Done after the
        //    per-sample chain so all channels share the same slice timing.
        if (stutterAmt > 1.0e-4f && stutterCap > 0)
        {
            for (int i = 0; i < n; ++i)
            {
                // On a slice boundary, start capturing a fresh slice (and rewind play).
                if (stutterPos >= stutterLen) { stutterPos = 0; stutterWrite = 0; stutterPlay = 0; }
                for (int ch = 0; ch < juce::jmin (numCh, 2); ++ch)
                {
                    float* d = buffer.getWritePointer (ch);
                    if (stutterWrite < stutterLen)
                        stutterBuf[(size_t) ch][(size_t) stutterWrite] = d[i];      // fill the slice
                    const int pIdx = stutterPlay % juce::jmax (1, stutterLen);
                    const float rep = stutterBuf[(size_t) ch][(size_t) pIdx];
                    d[i] = d[i] * (1.0f - stutterAmt) + rep * stutterAmt;
                }
                ++stutterWrite; ++stutterPlay; ++stutterPos;
            }
        }
        else { stutterWrite = stutterPlay = stutterPos = 0; }

        // Dry/wet blend (against the pre-FX dry copy) + output gain.
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            const float* dry = dryScratch.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float wet = juce::jlimit (-1.5f, 1.5f, d[i]);
                const float y = (wet * mix + dry[i] * (1.0f - mix)) * outGain;
                d[i] = y;
                peak = juce::jmax (peak, std::fabs (y));
            }
        }

        outLevel.store (juce::jlimit (0.0f, 1.5f, peak), std::memory_order_relaxed);
    }

    float currentOutLevel() const noexcept { return outLevel.load (std::memory_order_relaxed); }

private:
    // Male vowel formants (F1, F2) in Hz for A E I O U — the morph sweeps across them.
    static void vowelFormants (double morph, double& f1, double& f2) noexcept
    {
        constexpr int    kNumVowels = 5;
        constexpr double kVowelF1[kNumVowels] = { 730.0, 530.0, 270.0, 570.0, 300.0 };
        constexpr double kVowelF2[kNumVowels] = { 1090.0, 1840.0, 2290.0, 840.0, 870.0 };

        const double x = juce::jlimit (0.0, 1.0, morph) * (kNumVowels - 1);
        const int    i = juce::jlimit (0, kNumVowels - 2, (int) std::floor (x));
        const double t = x - i;
        f1 = kVowelF1[i] + (kVowelF1[i + 1] - kVowelF1[i]) * t;
        f2 = kVowelF2[i] + (kVowelF2[i + 1] - kVowelF2[i]) * t;
    }

    // --- One RBJ bandpass biquad (a formant resonator) ----------------------
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        void setBandpass (double sampleRate, double freq, double q) noexcept
        {
            freq = juce::jlimit (20.0, sampleRate * 0.45, freq);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
            const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.1, q));
            const double cw = std::cos (w0);
            const double a0 = 1.0 + alpha;
            b0 =  alpha / a0;   b1 = 0.0;   b2 = -alpha / a0;
            a1 = (-2.0 * cw) / a0;   a2 = (1.0 - alpha) / a0;
        }

        inline float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }

        void reset() noexcept { z1 = z2 = 0.0; }
    };

    float p (const char* id) const noexcept
    {
        auto* v = state->getRawParameterValue (pfx + id);
        return v != nullptr ? v->load() : 0.0f;
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    std::atomic<float> outLevel { 0.0f };
    juce::AudioBuffer<float> dryScratch;      // pre-FX copy for the dry/wet blend

    // Per-channel state (stereo).
    std::array<Biquad, 2> vowelF1, vowelF2;   // two resonators per channel
    std::array<double, 2> ringPhase { { 0.0, 0.0 } };
    std::array<float,  2> crushHold  { { 0.0f, 0.0f } };
    std::array<int,    2> crushCount { { 0, 0 } };

    // Stutter (beat-repeat) buffers, one per channel.
    std::array<std::vector<float>, 2> stutterBuf;
    int stutterCap = 0;         // max slice length in samples (allocated)
    int stutterWrite = 0;       // write cursor into the current capture
    int stutterPlay = 0;        // playback cursor within the captured slice
    int stutterLen = 0;         // current slice length
    int stutterPos = 0;         // samples since last slice boundary

    double hostBpm = 120.0;
};

} // namespace sattari::dsp
