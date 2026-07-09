#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id,
                                                       const juce::String& name,
                                                       float min, float max, float def,
                                                       float step = 0.001f,
                                                       const juce::String& suffix = {})
{
    juce::NormalisableRange<float> range (min, max, step);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
        juce::AudioParameterFloatAttributes().withLabel (suffix));
}

float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }

// Musical division -> beats (quarter-note = 1.0 beat) for the tempo-gate cycle.
double cycleBeatsFor (int div)
{
    switch (div)
    {
        case 0: return 4.0;         // 1/1
        case 1: return 2.0;         // 1/2
        case 2: return 1.0;         // 1/4
        case 3: return 0.5;         // 1/8
        case 4: return 1.0 / 3.0;   // 1/8T
        case 5: return 0.25;        // 1/16
        default: return 0.5;
    }
}
} // namespace

SattariGateAudioProcessor::SattariGateAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

SattariGateAudioProcessor::~SattariGateAudioProcessor() { tempoReader.stop(); }

juce::AudioProcessorValueTreeState::ParameterLayout
SattariGateAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (floatParam ("threshold", "Threshold", -80.0f, 0.0f, -40.0f, 0.1f, "dB"));
    // How much the gate attenuates the signal when fully closed.
    p.push_back (floatParam ("range", "Range", 0.0f, 90.0f, 60.0f, 0.1f, "dB"));
    // Expander ratio: 1 = pure hard gate (floor), higher = softer downward expander.
    p.push_back (floatParam ("ratio", "Ratio", 1.0f, 20.0f, 2.0f, 0.01f, ":1"));
    p.push_back (floatParam ("attack", "Attack", 0.05f, 100.0f, 1.0f, 0.05f, "ms"));
    p.push_back (floatParam ("hold", "Hold", 0.0f, 500.0f, 40.0f, 1.0f, "ms"));
    p.push_back (floatParam ("release", "Release", 5.0f, 2000.0f, 120.0f, 1.0f, "ms"));
    p.push_back (floatParam ("lookahead", "Lookahead", 0.0f, 10.0f, 2.0f, 0.1f, "ms"));
    // Dual-threshold gap: close threshold sits `hysteresis` dB below the open one.
    p.push_back (floatParam ("hysteresis", "Hysteresis", 0.0f, 24.0f, 4.0f, 0.1f, "dB"));

    p.push_back (floatParam ("scHpf", "SC HPF", 20.0f, 2000.0f, 20.0f, 1.0f, "Hz"));
    p.push_back (floatParam ("scLpf", "SC LPF", 500.0f, 20000.0f, 20000.0f, 1.0f, "Hz"));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("scListen", 1), "SC Listen", false));

    // Detector: peak vs RMS.
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("detMode", 1), "Detector",
        juce::StringArray { "Peak", "RMS" }, 0));
    // Expander vs pure gate.
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("expander", 1), "Expander", false));

    // Tempo-gate (rhythmic trance gate locked to the tempo bus).
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("tempoGate", 1), "Tempo Gate", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("tempoDiv", 1), "Rate",
        juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16" }, 3));
    p.push_back (floatParam ("tempoDuty", "Duty", 5.0f, 95.0f, 50.0f, 1.0f, "%"));

    return { p.begin(), p.end() };
}

void SattariGateAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;

    // Detector follower: a fast rectified peak follower. Attack short so the gate
    // reacts promptly; the gate's own attack/hold/release shapes the audible edge.
    detectorFollower.prepare (sampleRate, 0.5f, 5.0f);
    detectorFollower.reset (0.0f);
    rmsSquare = 0.0f;

    currentGateGain = 0.0f;
    holdCounter = 0;
    gateOpen = false;

    hpX1 = hpY1 = 0.0f;
    lpY1 = 0.0f;
    syncPhase = 0.0;

    gainReductionDb.store (0.0f);
    gateOpenPublished.store (false);
    tempoGateActive.store (false);

    // Lookahead line sized for the 10 ms max (stereo, allocation-free afterwards).
    maxLookaheadSamples = (int) std::ceil (0.010 * sampleRate);
    lookaheadRingLen = maxLookaheadSamples + 1;
    lookaheadRing.setSize (2, lookaheadRingLen);
    lookaheadRing.clear();
    lookaheadRingPos = 0;
    reportedLatency = -1;   // force a setLatencySamples() on the first block

    tempoReader.start();
}

void SattariGateAudioProcessor::releaseResources() { tempoReader.stop(); }

bool SattariGateAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

// One-pole HP followed by one-pole LP, forming the detector sidechain band-pass.
float SattariGateAudioProcessor::sidechainFilter (float input, float hpHz, float lpHz) noexcept
{
    float x = input;

    // High-pass (skip when effectively off, i.e. <= ~20 Hz).
    if (hpHz > 20.5f)
    {
        hpHz = juce::jlimit (20.0f, 2000.0f, hpHz);
        const float rc = 1.0f / (juce::MathConstants<float>::twoPi * hpHz);
        const float dt = 1.0f / (float) sampleRate;
        const float alpha = rc / (rc + dt);
        const float y = alpha * (hpY1 + x - hpX1);
        hpX1 = x;
        hpY1 = y;
        x = y;
    }
    else
    {
        hpX1 = x;
        hpY1 = x;
    }

    // Low-pass (skip when effectively wide open near Nyquist).
    const float nyquist = 0.5f * (float) sampleRate;
    if (lpHz < nyquist * 0.95f)
    {
        lpHz = juce::jlimit (100.0f, nyquist * 0.98f, lpHz);
        const float dt = 1.0f / (float) sampleRate;
        const float rc = 1.0f / (juce::MathConstants<float>::twoPi * lpHz);
        const float alpha = dt / (rc + dt);
        lpY1 += alpha * (x - lpY1);
        x = lpY1;
    }
    else
    {
        lpY1 = x;
    }

    return x;
}

double SattariGateAudioProcessor::resolveBpm()
{
    const auto t = sattari::host::read (*this);
    if (t.hasHostBpm && t.bpm > 1.0)
        return t.bpm;
    if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)
        return s.bpm;
    return 120.0;
}

void SattariGateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;

    const float thresholdDb = param ("threshold");
    const float rangeDb     = juce::jmax (0.0f, param ("range"));
    const float ratio       = juce::jmax (1.0f, param ("ratio"));
    const float attackMs    = param ("attack");
    const float holdMs      = juce::jmax (0.0f, param ("hold"));
    const float releaseMs   = param ("release");
    const float lookaheadMs = param ("lookahead");
    const float hysteresisDb = juce::jmax (0.0f, param ("hysteresis"));
    const float scHpfHz     = param ("scHpf");
    const float scLpfHz     = param ("scLpf");
    const bool  scListen    = boolParam ("scListen");
    const int   detMode     = choiceParam ("detMode");   // 0 = Peak, 1 = RMS
    const bool  expander    = boolParam ("expander");
    const bool  tempoGate   = boolParam ("tempoGate");
    const int   tempoDiv    = choiceParam ("tempoDiv");
    const float tempoDuty   = juce::jlimit (0.05f, 0.95f, param ("tempoDuty") * 0.01f);

    tempoGateActive.store (tempoGate);

    // --- Attack/release ramp coefficients (per-sample linear-in-log ramps). -----
    // A one-pole toward the target gate gain: coeff = exp(-1 / (sr * timeSec)).
    auto coeffFor = [this] (float ms) -> float
    {
        const float t = ms * 0.001f;
        return t > 0.0f ? std::exp (-1.0f / (float) (sampleRate * (double) t)) : 0.0f;
    };
    const float attackCoeff  = coeffFor (attackMs);
    const float releaseCoeff = coeffFor (releaseMs);
    const int   holdSamples  = (int) std::lround (holdMs * 0.001 * sampleRate);

    // Closed-gate floor gain (range dB of attenuation; range 0 => open, i.e. bypass).
    const float floorGain = dbToGain (-rangeDb);

    // Thresholds: open above `thresholdDb`, close below `thresholdDb - hysteresis`.
    const float openThreshDb  = thresholdDb;
    const float closeThreshDb = thresholdDb - hysteresisDb;

    // RMS averaging coefficient (~ a short window for a smooth level estimate).
    const float rmsCoeff = coeffFor (5.0f);

    // Lookahead: delay the main path so the (undelayed) detector can pre-open.
    const int lookaheadSamples = juce::jlimit (0, maxLookaheadSamples,
                                               (int) std::lround (lookaheadMs * 0.001 * sampleRate));
    if (lookaheadSamples != reportedLatency)
    {
        reportedLatency = lookaheadSamples;
        setLatencySamples (lookaheadSamples);
    }

    // --- Tempo-gate phase setup ------------------------------------------------
    double cyclesPerSample = 0.0;
    if (tempoGate)
    {
        const double bpm = resolveBpm();
        const double cycleBeats = cycleBeatsFor (tempoDiv);
        cyclesPerSample = (bpm / 60.0) / sampleRate / cycleBeats;

        // Lock phase to host PPQ while playing so the gate lands on the beat.
        const auto t = sattari::host::read (*this);
        if (t.isPlaying && t.hasPpq)
            syncPhase = std::fmod ((t.ppqPosition / cycleBeats), 1.0);
        if (syncPhase < 0.0) syncPhase += 1.0;
    }

    float grForMeter = 0.0f;
    bool  anyOpen = false;

    for (int i = 0; i < n; ++i)
    {
        // Stereo-linked detector: max of |L|,|R| keeps the image stable.
        float det = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            det = std::max (det, std::fabs (buffer.getReadPointer (ch)[i]));

        const float filtered = sidechainFilter (det, scHpfHz, scLpfHz);
        const float rectified = std::fabs (filtered);

        // Level estimate: peak follower or RMS.
        float level;
        if (detMode == 1)
        {
            rmsSquare = rectified * rectified + rmsCoeff * (rmsSquare - rectified * rectified);
            level = std::sqrt (juce::jmax (0.0f, rmsSquare));
        }
        else
        {
            level = detectorFollower.processRectified (filtered);
        }

        const float levelDb = juce::Decibels::gainToDecibels (level + 1.0e-9f, -120.0f);

        // --- Target gate gain ---------------------------------------------------
        float targetGain;

        if (tempoGate)
        {
            // Rhythmic trance-gate: open for the first `duty` fraction of each
            // musical cycle, closed for the rest. The attack/release ramp below
            // smooths the hard edges into clicks-free transitions.
            const bool inOpenWindow = (float) syncPhase < tempoDuty;
            gateOpen = inOpenWindow;
            targetGain = inOpenWindow ? 1.0f : floorGain;

            syncPhase += cyclesPerSample;
            if (syncPhase >= 1.0) syncPhase -= 1.0;
        }
        else
        {
            // --- Hysteresis state machine --------------------------------------
            if (! gateOpen)
            {
                if (levelDb >= openThreshDb)
                {
                    gateOpen = true;
                    holdCounter = holdSamples;   // arm hold on open
                }
            }
            else
            {
                if (levelDb >= closeThreshDb)
                {
                    // Still above the close point: refresh the hold window so the
                    // gate stays open (this is what prevents chatter around one
                    // threshold — the signal must fall below closeThreshDb AND the
                    // hold must expire before we close).
                    holdCounter = holdSamples;
                }
                else if (holdCounter > 0)
                {
                    --holdCounter;   // below close point but still holding open
                }
                else
                {
                    gateOpen = false;   // hold expired below the close point -> close
                }
            }

            if (gateOpen)
            {
                targetGain = 1.0f;
            }
            else if (expander)
            {
                // Downward expander: attenuation scales with how far below the
                // (open) threshold the level sits, softened by ratio, clamped to
                // the range floor. At the threshold -> ~unity; deep below -> floor.
                const float belowDb = juce::jmax (0.0f, openThreshDb - levelDb);
                const float reductionDb = juce::jmin (rangeDb, belowDb * (ratio - 1.0f));
                targetGain = dbToGain (-reductionDb);
            }
            else
            {
                targetGain = floorGain;   // hard gate: snap to the floor when closed
            }
        }

        // --- Attack/release ramp toward the target -----------------------------
        // Rising toward the target is "attack" (opening), falling is "release".
        const float c = (targetGain > currentGateGain) ? attackCoeff : releaseCoeff;
        currentGateGain = targetGain + c * (currentGateGain - targetGain);
        currentGateGain = juce::jlimit (0.0f, 1.0f, currentGateGain);

        anyOpen = anyOpen || gateOpen;
        const float grDb = -juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, currentGateGain), -120.0f);
        grForMeter = std::max (grForMeter, grDb);

        // --- Lookahead + apply the gain ----------------------------------------
        // Write the current sample into the ring, read the delayed one back out;
        // with lookahead 0 this reads exactly what we just wrote (unity delay).
        const int readPos = (lookaheadRingPos - lookaheadSamples + lookaheadRingLen) % lookaheadRingLen;
        for (int ch = 0; ch < numCh; ++ch)
        {
            const int ringCh = juce::jmin (ch, lookaheadRing.getNumChannels() - 1);
            const float cur = buffer.getReadPointer (ch)[i];
            lookaheadRing.setSample (ringCh, lookaheadRingPos, cur);
            const float delayed = lookaheadRing.getSample (ringCh, readPos);

            float outv;
            if (scListen)
                outv = filtered;                    // audition the detector signal
            else
                outv = delayed * currentGateGain;   // gated audio

            buffer.getWritePointer (ch)[i] = juce::jlimit (-1.5f, 1.5f, outv);
        }
        if (++lookaheadRingPos >= lookaheadRingLen) lookaheadRingPos = 0;
    }

    gainReductionDb.store (grForMeter, std::memory_order_relaxed);
    gateOpenPublished.store (anyOpen, std::memory_order_relaxed);
}

void SattariGateAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariGateAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariGateAudioProcessor::createEditor()
{
    return new SattariGateAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariGateAudioProcessor();
}
