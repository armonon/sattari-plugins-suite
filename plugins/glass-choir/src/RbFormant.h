#pragma once

// Compact real-time Rubber Band engine with independent pitch + formant scaling
// and a clean output FIFO. Copied verbatim (bit-for-bit DSP) from
// plugins/face/src/RbFormant.h so Glass Choir can carry its own compile guard
// (GLASSCHOIR_HAS_RUBBERBAND) without coupling to the Face plugin's define. See
// that file for the design notes (PitchHighConsistency / OptionEngineFiner /
// FormantPreserved + the output FIFO that hands the host exactly N samples).

#include <JuceHeader.h>
#include <vector>

#if GLASSCHOIR_HAS_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

namespace sattari::glasschoir
{

class RbFormant
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        prepared = false;
        blockSize = juce::jmax (1, maxBlock);
       #if GLASSCHOIR_HAS_RUBBERBAND
        using RB = RubberBand::RubberBandStretcher;
        const auto opts = RB::OptionProcessRealTime
                        | RB::OptionEngineFiner
                        | RB::OptionPitchHighConsistency
                        | RB::OptionFormantPreserved;
        stretcher = std::make_unique<RB> (sampleRate, 1, opts, 1.0, 1.0);
        stretcher->setMaxProcessSize ((size_t) blockSize);
        stretcher->setPitchScale (1.0);

        startDelay = (int) stretcher->getStartDelay();
        cushion = juce::jmax (blockSize, startDelay);
        cap = cushion + startDelay + 8 * blockSize + 16384;
        fifo.assign ((size_t) cap, 0.0f);
        drain.assign ((size_t) blockSize, 0.0f);
        rd = count = 0; primed = false;
        prepared = true;
       #else
        juce::ignoreUnused (sampleRate);
       #endif
    }

    int latencySamples() const noexcept
    {
       #if GLASSCHOIR_HAS_RUBBERBAND
        return prepared ? startDelay + cushion : 0;
       #else
        return 0;
       #endif
    }

    // Process n input samples -> n output samples. pitchScale and formantScale are
    // multiplicative (2.0 = up one octave). Falls back to dry copy without RB.
    void process (const float* in, float* out, int n, double pitchScale, double formantScale)
    {
       #if GLASSCHOIR_HAS_RUBBERBAND
        if (! prepared || stretcher == nullptr) { for (int i = 0; i < n; ++i) out[i] = in[i]; return; }
        stretcher->setPitchScale (juce::jlimit (0.25, 4.0, pitchScale));
        stretcher->setFormantScale (juce::jlimit (0.25, 4.0, formantScale));
        stretcher->process (&in, (size_t) n, false);

        for (;;)
        {
            const int avail = (int) stretcher->available();
            if (avail <= 0) break;
            const int chunk = juce::jmin (avail, (int) drain.size());
            float* d = drain.data();
            stretcher->retrieve (&d, (size_t) chunk);
            push (d, chunk);
        }

        if (! primed && count >= cushion) primed = true;
        if (! primed) { for (int i = 0; i < n; ++i) out[i] = 0.0f; return; }

        const int avail = juce::jmin (n, count);
        int r = rd;
        for (int i = 0; i < avail; ++i)  { out[i] = fifo[(size_t) r]; if (++r >= cap) r = 0; }
        for (int i = avail; i < n; ++i)  out[i] = 0.0f;
        rd = (rd + avail) % cap;
        count -= avail;
       #else
        juce::ignoreUnused (pitchScale, formantScale);
        for (int i = 0; i < n; ++i) out[i] = in[i];
       #endif
    }

private:
   #if GLASSCHOIR_HAS_RUBBERBAND
    void push (const float* src, int n)
    {
        if (count + n > cap) { const int drop = count + n - cap; rd = (rd + drop) % cap; count -= drop; }
        int w = (rd + count) % cap;
        for (int i = 0; i < n; ++i) { fifo[(size_t) w] = src[i]; if (++w >= cap) w = 0; }
        count += n;
    }

    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    std::vector<float> fifo, drain;
    int cap = 0, rd = 0, count = 0, cushion = 0, startDelay = 0;
    bool primed = false;
   #endif
    bool prepared = false;
    int blockSize = 512;
};

} // namespace sattari::glasschoir
