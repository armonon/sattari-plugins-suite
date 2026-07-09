#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::StringArray SattariEchoAudioProcessor::divisionChoices()
{ return { "1/4", "1/4 dot", "1/4 trip", "1/8", "1/8 dot", "1/8 trip", "1/16", "1/16 trip", "1/32" }; }

// Beats (quarter-note = 1.0) for each division index.
static double beatsFor (int div)
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

SattariEchoAudioProcessor::SattariEchoAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}
SattariEchoAudioProcessor::~SattariEchoAudioProcessor() { tempoReader.stop(); }

juce::AudioProcessorValueTreeState::ParameterLayout
SattariEchoAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    using BP = juce::AudioParameterBool;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<BP> ("sync", "Tempo Sync", true));
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("div", "Time Division", divisionChoices(), 3));
    p.push_back (std::make_unique<FP> ("timems", "Free Time",
        juce::NormalisableRange<float> (10.0f, 2000.0f, 1.0f, 0.4f), 350.0f));
    p.push_back (std::make_unique<FP> ("feedback", "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.001f), 0.4f));
    p.push_back (std::make_unique<BP> ("pingpong", "Ping-Pong", false));
    p.push_back (std::make_unique<FP> ("width", "Width",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    // Tone: 0 = neutral, negative darkens (LP damping), positive brightens (HP damping).
    p.push_back (std::make_unique<FP> ("tone", "Tone",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), -0.2f));
    p.push_back (std::make_unique<FP> ("diffusion", "Diffusion",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<FP> ("duck", "Ducking",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<FP> ("mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.3f));
    p.push_back (std::make_unique<FP> ("output", "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariEchoAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sampleRate = sr > 0.0 ? sr : 44100.0;
    tempoReader.start();

    // Max delay = 2 s free-time ceiling with headroom for a slow synced whole note.
    delayLen = (int) std::ceil (sampleRate * 2.2) + 8;
    for (auto& d : delayBuf) d.assign ((size_t) delayLen, 0.0f);
    delayWrite = 0;
    dampLpState = { 0.0f, 0.0f };

    // Diffusion all-pass: a short (~7 ms) fixed delay per channel.
    apLen = juce::jmax (16, (int) std::ceil (sampleRate * 0.007));
    for (auto& a : apBuf) a.assign ((size_t) apLen, 0.0f);
    apPos = { 0, 0 };

    duckFollower.prepare (sampleRate, 5.0f, 150.0f);
    duckFollower.reset (0.0f);

    smoothedDelaySamples = 0.0;   // seeded on the first block
}
void SattariEchoAudioProcessor::releaseResources() { tempoReader.stop(); }

bool SattariEchoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double SattariEchoAudioProcessor::resolveBpm()
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm(); bpm && *bpm > 1.0)
                return *bpm;
    if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)   // tempo bus fallback
        return s.bpm;
    return 120.0;
}

double SattariEchoAudioProcessor::resolveDelaySamples()
{
    double samples;
    if (param ("sync") > 0.5f)
    {
        const double bpm = lastBpm.load (std::memory_order_relaxed);
        const int div = (int) std::round (param ("div"));
        samples = beatsFor (div) * (60.0 / bpm) * sampleRate;
    }
    else
    {
        samples = (double) param ("timems") * 0.001 * sampleRate;
    }
    return juce::jlimit (1.0, (double) delayLen - 4.0, samples);
}

void SattariEchoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();

    const double bpm = resolveBpm();
    lastBpm.store (bpm, std::memory_order_relaxed);

    const double targetDelay = resolveDelaySamples();
    if (smoothedDelaySamples <= 0.0)
        smoothedDelaySamples = targetDelay;   // seed on the first block, no glide from 0

    const float fb        = juce::jlimit (0.0f, 0.95f, param ("feedback"));
    const bool  pingpong  = param ("pingpong") > 0.5f && numCh >= 2;
    const float width     = juce::jlimit (0.0f, 1.0f, param ("width"));
    const float tone      = juce::jlimit (-1.0f, 1.0f, param ("tone"));
    const float diffusion = juce::jlimit (0.0f, 1.0f, param ("diffusion"));
    const float duckAmt   = juce::jlimit (0.0f, 1.0f, param ("duck"));
    const float mix       = juce::jlimit (0.0f, 1.0f, param ("mix"));
    const float outGain   = juce::Decibels::decibelsToGain (param ("output"));

    // Tone as a one-pole damping coefficient in [0,1). Neutral (~tone 0) leaves the
    // repeats mostly intact; negative darkens (heavier LP), positive brightens by
    // subtracting the LP (a gentle HP shelf). dampMix picks LP vs HP contribution.
    const float dampCoeff = 0.5f - 0.45f * tone;          // 0.05..0.95
    const float hpAmount  = juce::jmax (0.0f, tone);      // brighten strength
    const float lpAmount  = juce::jmax (0.0f, -tone);     // darken strength

    // Diffusion all-pass coefficient (0 -> bypass, up to ~0.7 smear).
    const float apG = 0.7f * diffusion;

    // Ping-pong cross-feed: send each channel's feedback to the OTHER channel.
    // Slightly < unity total so it can never exceed the straight-feedback energy.
    const float ppCross = pingpong ? 1.0f : 0.0f;

    // Per-sample delay-time glide coefficient (~30 ms).
    const double glide = std::exp (-1.0 / (0.030 * sampleRate));

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

        if (L) L[i] = juce::jlimit (-1.5f, 1.5f, (buffer.getReadPointer (0)[i] * (1.0f - mix) + wetL * mix)) * outGain;
        if (R) R[i] = juce::jlimit (-1.5f, 1.5f, (buffer.getReadPointer (1)[i] * (1.0f - mix) + wetR * mix)) * outGain;

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

void SattariEchoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}
void SattariEchoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariEchoAudioProcessor::createEditor()
{
    return new SattariEchoAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariEchoAudioProcessor();
}
