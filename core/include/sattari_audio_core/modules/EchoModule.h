#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariEnvelopeFollower.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// =============================================================================
//  EchoModule — Sattari Echo's DSP, extracted so the standalone plugin and a
//  Create rack slot run the SAME delay.
//
//  A tempo-locked stereo delay: synced division or free ms, damped feedback with
//  a tone tilt (LP darken / HP brighten), an optional diffusion all-pass for tape
//  smear, ping-pong cross-feed, stereo widening and input-driven ducking.
//
//  Ping-pong crosses the FEEDBACK, not the first tap: the wet output is always
//  this channel's own tap, so a straight delay's first repeat lands where you
//  expect while the bounce still develops over successive repeats.
//
//  The feedback write is clamped, and any non-finite output is replaced with
//  silence — with feedback near 0.95 plus tone and diffusion in the loop, the
//  filter and all-pass gains can otherwise compound into a runaway.
// =============================================================================

namespace sattari::dsp
{

class EchoModule final : public SattariModule
{
public:
    ~EchoModule() override { tempoReader.stop(); }

    static juce::StringArray divisionChoices()
    { return { "1/4", "1/4 dot", "1/4 trip", "1/8", "1/8 dot", "1/8 trip", "1/16", "1/16 trip", "1/32" }; }

    const char* moduleId()    const noexcept override { return "echo"; }
    const char* displayName() const noexcept override { return "Echo"; }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FP = juce::AudioParameterFloat;
        using BP = juce::AudioParameterBool;
        using R  = juce::NormalisableRange<float>;

        out.push_back (std::make_unique<BP> (juce::ParameterID (prefix + "sync", 1), "Tempo Sync", true));
        out.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "div", 1), "Time Division", divisionChoices(), 3));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "timems", 1), "Free Time",
            R (10.0f, 2000.0f, 1.0f, 0.4f), 350.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "feedback", 1), "Feedback",
            R (0.0f, 0.95f, 0.001f), 0.4f));
        out.push_back (std::make_unique<BP> (juce::ParameterID (prefix + "pingpong", 1), "Ping-Pong", false));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "width", 1), "Width",
            R (0.0f, 1.0f, 0.01f), 1.0f));
        // Tone: 0 = neutral, negative darkens (LP damping), positive brightens (HP damping).
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "tone", 1), "Tone",
            R (-1.0f, 1.0f, 0.01f), -0.2f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "diffusion", 1), "Diffusion",
            R (0.0f, 1.0f, 0.01f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "duck", 1), "Ducking",
            R (0.0f, 1.0f, 0.01f), 0.0f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "mix", 1), "Mix",
            R (0.0f, 1.0f, 0.01f), 0.3f));
        out.push_back (std::make_unique<FP> (juce::ParameterID (prefix + "output", 1), "Output",
            R (-24.0f, 12.0f, 0.1f), 0.0f));
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    void prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        tempoReader.start();

        // Cover the 2 s free-time ceiling plus the longest synced value.
        // Longest sync value is a dotted quarter (1.5 beats). Reserve enough
        // history down to the suite's 20 BPM transport floor: 4.5 seconds.
        delayLen = (int) std::ceil (sr * 4.6) + 8;
        for (auto& d : delayBuf) d.assign ((size_t) delayLen, 0.0f);

        // Diffusion all-pass: a short (~7 ms) fixed delay per channel.
        apLen = juce::jmax (16, (int) std::ceil (sr * 0.007));
        for (auto& a : apBuf) a.assign ((size_t) apLen, 0.0f);

        duckFollower.prepare (sr, 5.0f, 150.0f);
        reset();
    }

    // A delay is exactly the case the pure-virtual reset() exists for: without it a
    // transport jump replays the previous position's repeats over the new one.
    void reset() override
    {
        for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& a : apBuf)    std::fill (a.begin(), a.end(), 0.0f);
        delayWrite = 0;
        apPos = { { 0, 0 } };
        dampLpState = { { 0.0f, 0.0f } };
        duckFollower.reset (0.0f);
        smoothedDelaySamples = 0.0;   // seeded on the first block
        lastBpm.store (120.0, std::memory_order_relaxed);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override { transport = t; }

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        if (state == nullptr) return;
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0) return;

        const double bpm = resolveBpm();
        lastBpm.store (bpm, std::memory_order_relaxed);

        const double targetDelay = resolveDelaySamples (bpm);
        if (smoothedDelaySamples <= 0.0)
            smoothedDelaySamples = targetDelay;   // seed on the first block, no glide from 0

        const float fb        = juce::jlimit (0.0f, 0.95f, p ("feedback"));
        const bool  pingpong  = p ("pingpong") > 0.5f && numCh >= 2;
        const float width     = juce::jlimit (0.0f, 1.0f, p ("width"));
        const float tone      = juce::jlimit (-1.0f, 1.0f, p ("tone"));
        const float diffusion = juce::jlimit (0.0f, 1.0f, p ("diffusion"));
        const float duckAmt   = juce::jlimit (0.0f, 1.0f, p ("duck"));
        const float mix       = juce::jlimit (0.0f, 1.0f, p ("mix"));
        const float outGain   = juce::Decibels::decibelsToGain (p ("output"));

        // Tone as a one-pole damping coefficient in [0,1). Neutral (~tone 0) leaves the
        // repeats mostly intact; negative darkens (heavier LP), positive brightens by
        // subtracting the LP (a gentle HP shelf). dampMix picks LP vs HP contribution.
        const float dampCoeff = 0.5f - 0.45f * tone;          // 0.05..0.95
        const float hpAmount  = juce::jmax (0.0f, tone);      // brighten strength
        const float lpAmount  = juce::jmax (0.0f, -tone);     // darken strength

        // Diffusion all-pass coefficient (0 -> bypass, up to ~0.7 smear).
        const float apG = 0.7f * diffusion;

        // Ping-pong cross-feed: send each channel's feedback to the OTHER channel.
        const float ppCross = pingpong ? 1.0f : 0.0f;

        // Per-sample delay-time glide coefficient (~30 ms).
        const double glide = std::exp (-1.0 / (0.030 * sr));

        float* L = numCh > 0 ? buffer.getWritePointer (0) : nullptr;
        float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < n; ++i)
        {
            // Glide the read distance toward the target.
            smoothedDelaySamples = targetDelay + glide * (smoothedDelaySamples - targetDelay);
            const double rp = (double) delayWrite - smoothedDelaySamples;

            // Fetch the fractional-delay tap per channel.
            float delayed[2] = { 0.0f, 0.0f };
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& d = delayBuf[(size_t) ch];
                double r = rp;
                while (r < 0.0) r += (double) delayLen;
                int r0 = (int) r; double fr = r - (double) r0;
                if (r0 >= delayLen) r0 -= delayLen;
                int r1 = r0 + 1; if (r1 >= delayLen) r1 -= delayLen;
                delayed[ch] = d[(size_t) r0] * (1.0f - (float) fr) + d[(size_t) r1] * (float) fr;
            }

            // Duck detector from the dry input (mono sum of |L|,|R|).
            float det = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                det = juce::jmax (det, std::fabs (buffer.getReadPointer (ch)[i]));
            const float env = duckFollower.processRectified (det);
            // Ducking gain: at duckAmt=1 a strong input pulls the wet down to ~0.
            const float duckGain = 1.0f - duckAmt * juce::jmin (1.0f, env * 4.0f);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& d = delayBuf[(size_t) ch];
                const float in = buffer.getReadPointer (ch)[i];

                // The repeat energy re-entering the loop is this channel's tap for a
                // straight delay, or the OTHER channel's tap for ping-pong.
                const int srcCh = (pingpong ? (ch ^ 1) : ch);
                float loop = ppCross > 0.0f ? delayed[srcCh] : delayed[ch];

                // Tone: one-pole low-pass, then blend LP (darken) / (in-LP) (brighten).
                auto& lp = dampLpState[(size_t) ch];
                lp += dampCoeff * (loop - lp);
                const float lowpassed = lp;
                const float highpassed = loop - lp;
                loop = loop + lpAmount * (lowpassed - loop) + hpAmount * (highpassed);

                // Diffusion all-pass (tape smear) on the feedback path.
                if (apG > 0.0f)
                {
                    auto& ap = apBuf[(size_t) ch];
                    const int pos = apPos[(size_t) ch];
                    const float bufv = ap[(size_t) pos];
                    const float apOut = -apG * loop + bufv;
                    ap[(size_t) pos] = loop + apG * apOut;
                    loop = apOut;
                }

                // Write input + damped/diffused feedback into the line. Clamp the loop
                // state so a near-max feedback with any tone/diffusion still cannot run away.
                const float writeVal = in + juce::jlimit (-4.0f, 4.0f, loop * fb);
                d[(size_t) delayWrite] = writeVal;
            }

            // Advance the diffusion all-pass positions once per sample per channel.
            for (int ch = 0; ch < numCh; ++ch)
                if (apG > 0.0f)
                    if (++apPos[(size_t) ch] >= apLen) apPos[(size_t) ch] = 0;

            // Output = dry + wet(mix), with the wet ducked and (for stereo) widened.
            // Wet is the direct tap of this channel (not the cross-fed loop tap) so the
            // first repeat lands where you expect for a straight delay; ping-pong still
            // bounces because the FEEDBACK crosses channels.
            float wetL = numCh > 0 ? delayed[0] : 0.0f;
            float wetR = numCh > 1 ? delayed[1] : wetL;
            if (numCh >= 2)
            {
                const float mid  = 0.5f * (wetL + wetR);
                const float side = 0.5f * (wetL - wetR) * width;
                wetL = mid + side;
                wetR = mid - side;
            }
            wetL *= duckGain;
            wetR *= duckGain;

            const float dryL = numCh > 0 ? buffer.getReadPointer (0)[i] : 0.0f;
            const float dryR = numCh > 1 ? buffer.getReadPointer (1)[i] : 0.0f;
            if (L) L[i] = juce::jlimit (-1.5f, 1.5f, (dryL * (1.0f - mix) + wetL * mix)) * outGain;
            if (R) R[i] = juce::jlimit (-1.5f, 1.5f, (dryR * (1.0f - mix) + wetR * mix)) * outGain;

            if (++delayWrite >= delayLen) delayWrite = 0;
        }

        // Final safety: replace any non-finite output with silence (belt-and-braces).
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (out[i]))
                    out[i] = 0.0f;
        }
    }

    double currentBpm() const noexcept { return lastBpm.load (std::memory_order_relaxed); }

private:
    // Beats (quarter-note = 1.0) for each division index.
    static double beatsFor (int div) noexcept
    {
        switch (div)
        {
            case 0: return 1.0;         // 1/4
            case 1: return 1.5;         // 1/4 dotted
            case 2: return 2.0 / 3.0;   // 1/4 triplet
            case 3: return 0.5;         // 1/8
            case 4: return 0.75;        // 1/8 dotted
            case 5: return 1.0 / 3.0;   // 1/8 triplet
            case 6: return 0.25;        // 1/16
            case 7: return 1.0 / 6.0;   // 1/16 triplet
            case 8: return 0.125;       // 1/32
            default: return 0.5;
        }
    }

    double resolveBpm() noexcept
    {
        if (transport.hasHostBpm && transport.bpm > 1.0) return transport.bpm;
        if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0) return s.bpm;   // tempo bus fallback
        return 120.0;
    }

    double resolveDelaySamples (double bpm) noexcept
    {
        double samples;
        if (p ("sync") > 0.5f)
            samples = beatsFor ((int) std::round (p ("div"))) * (60.0 / bpm) * sr;
        else
            samples = (double) p ("timems") * 0.001 * sr;
        return juce::jlimit (1.0, (double) delayLen - 4.0, samples);
    }

    float p (const char* id) const noexcept
    {
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    double sr = 44100.0;

    sattari::xplugin::TempoReader tempoReader;
    sattari::host::TransportSnapshot transport;
    std::atomic<double> lastBpm { 120.0 };

    // Stereo delay lines (preallocated to a max time in prepare()).
    std::array<std::vector<float>, 2> delayBuf;
    int delayLen = 0, delayWrite = 0;

    // Smoothed delay time so retimes/division changes glide instead of clicking.
    double smoothedDelaySamples = 0.0;

    // Per-channel feedback-loop damping (one-pole LP/HP blend) state.
    std::array<float, 2> dampLpState { { 0.0f, 0.0f } };

    // Per-channel diffusion all-pass (single stage, short fixed delay) for tape smear.
    std::array<std::vector<float>, 2> apBuf;
    int apLen = 0;
    std::array<int, 2> apPos { { 0, 0 } };

    // Input-driven ducking follower (drives the wet down under the dry).
    EnvelopeFollower duckFollower;
};

} // namespace sattari::dsp
