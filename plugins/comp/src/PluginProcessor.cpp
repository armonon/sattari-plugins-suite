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

// Release-division choices for the tempo-release mode (quarter-note = 1.0 beat).
double releaseBeatsFor (int div)
{
    switch (div)
    {
        case 0: return 0.25;        // 1/16
        case 1: return 1.0 / 3.0;   // 1/8 triplet
        case 2: return 0.5;         // 1/8
        case 3: return 0.75;        // 1/8 dotted
        case 4: return 1.0;         // 1/4
        case 5: return 1.5;         // 1/4 dotted
        case 6: return 2.0;         // 1/2
        default: return 0.5;
    }
}
} // namespace

SattariCompAudioProcessor::SattariCompAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

SattariCompAudioProcessor::~SattariCompAudioProcessor() { tempoReader.stop(); }

juce::AudioProcessorValueTreeState::ParameterLayout
SattariCompAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (floatParam ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, 0.1f, "dB"));
    p.push_back (floatParam ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, 0.01f, ":1"));
    // Knee width in dB (0 = hard, up to a wide soft knee).
    p.push_back (floatParam ("knee", "Knee", 0.0f, 24.0f, 6.0f, 0.1f, "dB"));
    p.push_back (floatParam ("attack", "Attack", 0.1f, 120.0f, 8.0f, 0.1f, "ms"));
    p.push_back (floatParam ("release", "Release", 20.0f, 1500.0f, 140.0f, 1.0f, "ms"));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("autoRelease", 1), "Auto Release", false));
    p.push_back (floatParam ("makeup", "Makeup", -12.0f, 24.0f, 0.0f, 0.1f, "dB"));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("autoGain", 1), "Auto Gain", false));
    p.push_back (floatParam ("mix", "Mix", 0.0f, 100.0f, 100.0f, 0.1f, "%"));
    p.push_back (floatParam ("scHpf", "SC HPF", 20.0f, 400.0f, 20.0f, 1.0f, "Hz"));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("scListen", 1), "SC Listen", false));

    // Tempo-release: release time comes from a musical division off the tempo bus.
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("tempoRelease", 1), "Tempo Release", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("releaseDiv", 1), "Release Div",
        juce::StringArray { "1/16", "1/8T", "1/8", "1/8 dot", "1/4", "1/4 dot", "1/2" }, 2));

    return { p.begin(), p.end() };
}

void SattariCompAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    // The follower's coefficients are recomputed per-block from attack/release,
    // so prepare just fixes the sample rate and resets state.
    grSmoother.prepare (sampleRate, 8.0f, 140.0f);
    grSmoother.reset (0.0f);
    smoothedGrDb = 0.0f;
    hpX1 = hpY1 = 0.0f;
    gainReductionDb.store (0.0f);
    effectiveReleaseMs.store (140.0);
    tempoReleaseActive.store (false);
    tempoReader.start();
}

void SattariCompAudioProcessor::releaseResources() { tempoReader.stop(); }

bool SattariCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

float SattariCompAudioProcessor::detectorHighPass (float input, float cutoffHz) noexcept
{
    // Below ~20 Hz the HP is effectively bypassed (nothing musical to remove).
    if (cutoffHz <= 20.5f)
    {
        hpX1 = input;
        hpY1 = input;
        return input;
    }
    cutoffHz = juce::jlimit (20.0f, 400.0f, cutoffHz);
    const float rc = 1.0f / (juce::MathConstants<float>::twoPi * cutoffHz);
    const float dt = 1.0f / (float) sampleRate;
    const float alpha = rc / (rc + dt);
    const float y = alpha * (hpY1 + input - hpX1);
    hpX1 = input;
    hpY1 = y;
    return y;
}

double SattariCompAudioProcessor::resolveBpm()
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm(); bpm && *bpm > 1.0)
                return *bpm;
    if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)   // cross-plugin tempo bus
        return s.bpm;
    return 120.0;   // final fallback
}

double SattariCompAudioProcessor::resolveReleaseMs (double userReleaseMs)
{
    if (! boolParam ("tempoRelease"))
    {
        tempoReleaseActive.store (false);
        return userReleaseMs;
    }
    tempoReleaseActive.store (true);
    const double bpm = resolveBpm();
    const double beats = releaseBeatsFor (choiceParam ("releaseDiv"));
    const double ms = beats * (60000.0 / bpm);   // beats -> ms at this tempo
    return juce::jlimit (20.0, 3000.0, ms);
}

void SattariCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    const int n     = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;

    const float thresholdDb = param ("threshold");
    const float ratio       = juce::jmax (1.0f, param ("ratio"));
    const float kneeDb      = juce::jmax (0.0f, param ("knee"));
    const float attackMs    = param ("attack");
    const float userRelMs   = param ("release");
    const bool  autoRelease = boolParam ("autoRelease");
    const float makeupDb    = param ("makeup");
    const bool  autoGain    = boolParam ("autoGain");
    const float mix         = juce::jlimit (0.0f, 1.0f, param ("mix") * 0.01f);
    const float scHpfHz     = param ("scHpf");
    const bool  scListen    = boolParam ("scListen");

    // Effective release: tempo-release division overrides the ms knob when armed;
    // auto-release then multiplies it to give a slower program-dependent recovery.
    double releaseMs = resolveReleaseMs ((double) userRelMs);
    if (autoRelease)
        releaseMs = juce::jlimit (20.0, 3000.0, releaseMs * 1.8);
    effectiveReleaseMs.store (releaseMs);

    grSmoother.setTimes (attackMs, (float) releaseMs);

    // Auto make-up: estimate the static gain lost at the threshold and add it
    // back so louder-into-the-comp still lands near unity. Manual makeup adds on
    // top. (A gentle 0.5 factor keeps it musical rather than a full restore.)
    const float slope = 1.0f - 1.0f / ratio;              // 0..~0.95
    const float autoMakeupDb = autoGain ? 0.5f * slope * (-thresholdDb) : 0.0f;
    const float makeupGain = dbToGain (makeupDb + autoMakeupDb);

    const float invRatio = 1.0f / ratio;
    const float halfKnee = 0.5f * kneeDb;

    float grForMeter = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // Stereo-linked detector: max of |L|,|R| keeps the image stable.
        float det = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            det = std::max (det, std::fabs (buffer.getReadPointer (ch)[i]));

        const float filtered = detectorHighPass (det, scHpfHz);
        const float detMag = std::fabs (filtered);
        const float detDb = juce::Decibels::gainToDecibels (detMag + 1.0e-9f, -120.0f);

        // --- Log-domain gain computer (soft knee) --------------------------
        // targetGrDb <= 0 : how much the output should be reduced at this level.
        const float over = detDb - thresholdDb;
        float targetGrDb;
        if (kneeDb > 0.0001f && over > -halfKnee && over < halfKnee)
        {
            // Quadratic soft-knee interpolation across the +/- halfKnee region.
            const float x = over + halfKnee;               // 0 .. kneeDb
            const float outAboveDb = (1.0f - invRatio) * (x * x) / (2.0f * kneeDb);
            targetGrDb = -outAboveDb;
        }
        else if (over <= -halfKnee)
        {
            targetGrDb = 0.0f;                              // below the knee: no reduction
        }
        else
        {
            targetGrDb = -(over) * (1.0f - invRatio);       // hard-knee region above
        }

        // --- Attack/release smoothing on the GR envelope (dB) --------------
        // processFalling(): fast toward MORE reduction (more-negative), slow on
        // recovery — the natural compressor behaviour.
        smoothedGrDb = grSmoother.processFalling (targetGrDb);
        const float compGain = dbToGain (smoothedGrDb);

        grForMeter = std::max (grForMeter, -smoothedGrDb);

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = buffer.getReadPointer (ch)[i];
            if (scListen)
            {
                // Solo the (high-passed) detector signal so the user can audition
                // exactly what the compressor is keying off.
                buffer.getWritePointer (ch)[i] = juce::jlimit (-1.5f, 1.5f, filtered);
                continue;
            }
            const float wet  = dry * compGain * makeupGain;   // gain-reduced + makeup
            const float outv = dry * (1.0f - mix) + wet * mix; // parallel blend
            buffer.getWritePointer (ch)[i] = juce::jlimit (-4.0f, 4.0f, outv);
        }
    }

    gainReductionDb.store (grForMeter, std::memory_order_relaxed);
}

void SattariCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void SattariCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariCompAudioProcessor::createEditor()
{
    return new SattariCompAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariCompAudioProcessor();
}
