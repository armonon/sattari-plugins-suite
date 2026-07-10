#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include "FilterProcessor.h"

// =============================================================================
//  MasterFx — the DSP behind the "GLOBAL MASTER EFFECTS" XY pad.
// -----------------------------------------------------------------------------
//  X (0..1) → bipolar DJ filter sweep on the dry master path
//              0.5 = neutral, < 0.5 = high-pass, > 0.5 = low-pass
//  Y (0..1) → space/echo send amount (feedback echo cascaded into a reverb)
//              0.0 = fully dry, 1.0 = maximum wet
//  Neutral point (0.5, 0.0) is fully transparent, so the pad is always "on"
//  without needing a bypass toggle.
// =============================================================================
class MasterFx
{
public:
    void prepare (double sampleRate, int blockSize, int /*channels*/)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        filter.prepare (sr);

        // Pre-allocate the wet scratch to the max block size so process() never
        // reallocates on the audio thread (setSize below keeps this capacity).
        maxBlock = juce::jmax (1, blockSize);
        wetBuffer.setSize (2, maxBlock);

        const int maxDelay = (int) (sr * 0.75) + 4;
        for (auto& line : delayLines)
            line.assign ((size_t) juce::jmax (4, maxDelay), 0.0f);
        writePos = 0;

        reverb.setSampleRate (sr);
        juce::Reverb::Parameters p;
        p.roomSize   = 0.78f;
        p.damping    = 0.42f;
        p.width      = 1.0f;
        p.wetLevel   = 0.55f;   // wet buffer already carries the echo (dry)
        p.dryLevel   = 0.60f;
        p.freezeMode = 0.0f;
        reverb.setParameters (p);
    }

    void reset()
    {
        filter.reset();
        for (auto& line : delayLines)
            std::fill (line.begin(), line.end(), 0.0f);
        reverb.reset();
        writePos = 0;
    }

    // Stereo (or mono) master buffer, processed in place. x, y in [0, 1].
    void process (juce::AudioBuffer<float>& buffer, float x, float y)
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh      = juce::jmin (2, buffer.getNumChannels());
        if (numCh < 1 || numSamples <= 0 || delayLines[0].empty())
            return;

        const float filterControl = juce::jlimit (-1.0f, 1.0f, (x - 0.5f) * 2.0f);
        const float send          = juce::jlimit (0.0f, 1.0f, y);

        // ── DJ filter on the dry master path ────────────────────────────────
        {
            float* L = buffer.getWritePointer (0);
            float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int i = 0; i < numSamples; ++i)
            {
                float l = L[i];
                float r = R != nullptr ? R[i] : l;
                filter.processSample (l, r, filterControl);
                L[i] = l;
                if (R != nullptr) R[i] = r;
            }
        }

        if (send <= 0.0005f)
            return;

        // ── Space/Echo send: feedback echo → reverb, mixed back by `send` ───
        // avoidReallocating=true: no heap traffic on the audio thread as long as
        // numSamples <= maxBlock (pre-sized in prepare()).
        wetBuffer.setSize (numCh, numSamples, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
            wetBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        const int   lineLen      = (int) delayLines[0].size();
        const int   delaySamples = juce::jlimit (1, lineLen - 1, (int) (sr * 0.28));
        const float feedback     = 0.42f;

        int wp = writePos;
        for (int i = 0; i < numSamples; ++i)
        {
            int rp = wp - delaySamples;
            if (rp < 0) rp += lineLen;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto&  line = delayLines[(size_t) ch];
                float* w    = wetBuffer.getWritePointer (ch);
                const float delayed = line[(size_t) rp];
                line[(size_t) wp] = w[i] + delayed * feedback;
                w[i] = delayed;   // echo tap
            }
            if (++wp >= lineLen) wp = 0;
        }
        writePos = wp;

        if (numCh >= 2)
            reverb.processStereo (wetBuffer.getWritePointer (0), wetBuffer.getWritePointer (1), numSamples);
        else
            reverb.processMono (wetBuffer.getWritePointer (0), numSamples);

        // Aux-style: add the wet tail on top of the dry (filtered) master.
        const float wetGain = send * 0.9f;
        for (int ch = 0; ch < numCh; ++ch)
            buffer.addFrom (ch, 0, wetBuffer, ch, 0, numSamples, wetGain);
    }

private:
    double sr = 44100.0;
    int maxBlock = 0;
    FilterProcessor filter;
    juce::Reverb reverb;
    std::array<std::vector<float>, 2> delayLines;
    int writePos = 0;
    juce::AudioBuffer<float> wetBuffer;

    JUCE_LEAK_DETECTOR (MasterFx)
};
