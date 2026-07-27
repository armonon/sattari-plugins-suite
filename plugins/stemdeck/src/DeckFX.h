#pragma once

// Simple per-deck / per-channel built-in FX (reverb + echo). Extracted from
// MainPluginProcessor so a Channel/TrackDeckBody can own one without depending on
// the whole processor (Step 1b/3 of the channel re-architecture). RT-safe process:
// pre-allocated scratch + ring buffers, no allocation on the audio thread.

#include <JuceHeader.h>
#include <vector>

struct DeckFX
{
    juce::Reverb reverb;
    std::vector<float> echoL, echoR;
    int echoWritePos = 0;
    juce::AudioBuffer<float> wetScratch;   // pre-allocated reverb wet path

    void prepare (double sampleRate, int blockSize = 0)
    {
        juce::Reverb::Parameters p;
        p.roomSize = 0.6f;  p.damping = 0.5f;  p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;  p.width = 0.8f;     p.freezeMode = 0.0f;
        reverb.setParameters (p);
        reverb.setSampleRate (sampleRate);
        const int echoLen = (int) (sampleRate * 0.5);  // 500 ms max
        echoL.assign ((size_t) echoLen, 0.0f);
        echoR.assign ((size_t) echoLen, 0.0f);
        echoWritePos = 0;
        wetScratch.setSize (2, juce::jmax (1, blockSize));
    }

    void process (juce::AudioBuffer<float>& buf, float reverbWet, float echoWet)
    {
        if (buf.getNumSamples() == 0) return;
        const int n = buf.getNumSamples();
        float* L = buf.getWritePointer (0);
        float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;

        // Echo (300 ms tap, feedback 0.35)
        if (echoWet > 0.001f && ! echoL.empty())
        {
            const int echoLen = (int) echoL.size();
            const int tapOffset = (int) (echoLen * 0.6f);  // ~300 ms
            for (int i = 0; i < n; ++i)
            {
                const int readPos = ((echoWritePos - tapOffset) + echoLen) % echoLen;
                const float dL = echoL[(size_t) readPos];
                const float dR = echoR[(size_t) readPos];
                echoL[(size_t) echoWritePos] = L[i] + dL * 0.35f;
                echoR[(size_t) echoWritePos] = R[i] + dR * 0.35f;
                L[i] += dL * echoWet;
                R[i] += dR * echoWet;
                echoWritePos = (echoWritePos + 1) % echoLen;
            }
        }

        // Reverb (wet path reuses the pre-allocated scratch — no RT alloc)
        if (reverbWet > 0.001f)
        {
            const int wetCh = juce::jmin (2, buf.getNumChannels());
            wetScratch.setSize (juce::jmax (1, wetCh), n, false, false, true);
            for (int ch = 0; ch < wetCh; ++ch)
                wetScratch.copyFrom (ch, 0, buf, ch, 0, n);
            reverb.processStereo (wetScratch.getWritePointer (0),
                                  wetCh > 1 ? wetScratch.getWritePointer (1) : wetScratch.getWritePointer (0),
                                  n);
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.addFrom (ch, 0, wetScratch, juce::jmin (ch, wetCh - 1), 0, n, reverbWet);
        }
    }
};
