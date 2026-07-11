#include "VocalMasterProcessor.h"
#include "VocalMasterEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
namespace IIR = juce::dsp::IIR;
using ArrayCoeffs = juce::dsp::IIR::ArrayCoefficients<float>;

APVTS::ParameterLayout SattariVocalMasterAudioProcessor::createParameterLayout()
{
    using FloatP = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "input",  1 }, "Input",  juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "isolate",1 }, "Isolate (music down)", unit(), 0.3f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "denoise",1 }, "De-Noise", unit(), 0.3f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "deess",  1 }, "De-Ess",  unit(), 0.4f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "tone",   1 }, "Tone",    juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.1f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "air",    1 }, "Air",     unit(), 0.4f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "level",  1 }, "Leveler", unit(), 0.5f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "mix",    1 }, "Mix",     unit(), 1.0f));
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "output", 1 }, "Output",  juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

    return { p.begin(), p.end() };
}

SattariVocalMasterAudioProcessor::SattariVocalMasterAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

void SattariVocalMasterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    juce::dsp::ProcessSpec mono { currentSampleRate, (juce::uint32) juce::jmax (32, samplesPerBlock), 1 };

    for (int ch = 0; ch < 2; ++ch)
    {
        hpf[(size_t) ch].prepare (mono);
        tiltLow[(size_t) ch].prepare (mono);
        tiltHigh[(size_t) ch].prepare (mono);
        airShelf[(size_t) ch].prepare (mono);
        hpf[(size_t) ch].coefficients = IIR::Coefficients<float>::makeHighPass (currentSampleRate, 85.0f);
        tiltLow[(size_t) ch].coefficients = IIR::Coefficients<float>::makeLowShelf (currentSampleRate, 250.0f, 0.7f, 1.0f);
        tiltHigh[(size_t) ch].coefficients = IIR::Coefficients<float>::makeHighShelf (currentSampleRate, 4000.0f, 0.7f, 1.0f);
        airShelf[(size_t) ch].coefficients = IIR::Coefficients<float>::makeHighShelf (currentSampleRate, 11000.0f, 0.7f, 1.0f);
        deessEnv[(size_t) ch].prepare (currentSampleRate, 1.0f, 60.0f);
    }

    juce::dsp::ProcessSpec stereo { currentSampleRate, (juce::uint32) juce::jmax (32, samplesPerBlock), 2 };
    deessSplit.prepare (stereo);
    deessSplit.setCutoffFrequency (6500.0f);
    deessSplit.reset();

    gateEnv.prepare (currentSampleRate, 5.0f, 80.0f);
    gateGain.prepare (currentSampleRate, 5.0f, 60.0f);
    gateGain.reset (1.0f);
    levelEnv.prepare (currentSampleRate, 60.0f, 350.0f);
}

bool SattariVocalMasterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SattariVocalMasterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int n  = buffer.getNumSamples();
    const int nc = buffer.getNumChannels();
    if (n <= 0) return;

    const float inG     = juce::Decibels::decibelsToGain (param ("input"));
    const float outG    = juce::Decibels::decibelsToGain (param ("output"));
    const float isolate = param ("isolate");
    const float denoise = param ("denoise");
    const float deess   = param ("deess");
    const float airAmt  = param ("air");
    const float levelAmt= param ("level");
    const float mix     = param ("mix");

    // Per-block filter coefficients, written into existing coefficient objects.
    float lowDb, highDb; sattari::vocal::tiltGainsDb (param ("tone"), lowDb, highDb);
    const auto loC  = ArrayCoeffs::makeLowShelf  (currentSampleRate, 250.0f,   0.7f, juce::Decibels::decibelsToGain (lowDb));
    const auto hiC  = ArrayCoeffs::makeHighShelf (currentSampleRate, 4000.0f,  0.7f, juce::Decibels::decibelsToGain (highDb));
    const auto airC = ArrayCoeffs::makeHighShelf (currentSampleRate, 11000.0f, 0.7f, juce::Decibels::decibelsToGain (airAmt * 6.0f));
    for (int ch = 0; ch < 2; ++ch)
    {
        *tiltLow[(size_t) ch].coefficients  = loC;
        *tiltHigh[(size_t) ch].coefficients = hiC;
        *airShelf[(size_t) ch].coefficients = airC;
    }

    const float gateThr   = juce::Decibels::decibelsToGain (-60.0f + denoise * 40.0f);
    const float gateFloor = 1.0f - denoise * 0.94f;
    const float deThr     = 0.02f;
    const float targetRms = juce::Decibels::decibelsToGain (-18.0f);
    constexpr float ceiling = 0.98f;

    inputMeter.store (buffer.getMagnitude (0, n));

    float* chL = buffer.getWritePointer (0);
    float* chR = nc > 1 ? buffer.getWritePointer (1) : nullptr;

    float grPeak = 0.0f, gatePeak = 0.0f;

    auto chain = [&] (int ch, float x)
    {
        x = hpf[(size_t) ch].processSample (x);
        x = tiltLow[(size_t) ch].processSample (x);
        x = tiltHigh[(size_t) ch].processSample (x);
        x = airShelf[(size_t) ch].processSample (x);
        return x;
    };
    auto deessProc = [&] (int ch, float x)
    {
        float lo = 0.0f, hi = 0.0f;
        deessSplit.processSample (ch, x, lo, hi);
        const float e = deessEnv[(size_t) ch].processRectified (hi);
        float gr = 0.0f;
        if (e > deThr) gr = juce::jmin (0.9f, (e - deThr) / (e + 1.0e-6f) * deess);
        grPeak = juce::jmax (grPeak, gr);
        return lo + hi * (1.0f - gr);
    };

    for (int i = 0; i < n; ++i)
    {
        float L = chL[i] * inG;
        float R = chR ? chR[i] * inG : L;
        const float dryL = chR ? chL[i] : chL[i];
        const float dryR = chR ? chR[i] : chL[i];

        if (chR) sattari::vocal::isolateCentre (L, R, isolate, L, R);

        L = chain (0, L);
        if (chR) R = chain (1, R);

        L = deessProc (0, L);
        if (chR) R = deessProc (1, R);

        // De-noise gate (broadband, driven by the louder channel).
        const float det = juce::jmax (std::fabs (L), chR ? std::fabs (R) : std::fabs (L));
        const float ge  = gateEnv.processRectified (det);
        const float g   = gateGain.process (sattari::vocal::expanderGain (ge, gateThr, gateFloor));
        L *= g; if (chR) R *= g;
        gatePeak = juce::jmax (gatePeak, 1.0f - g);

        // Auto-leveler toward a target RMS.
        const float le     = levelEnv.processRectified (det);
        const float makeup = juce::jlimit (0.5f, 3.0f, le > 1.0e-5f ? targetRms / le : 1.0f);
        const float lvlG   = 1.0f + levelAmt * (makeup - 1.0f);
        L *= lvlG; if (chR) R *= lvlG;

        // Soft limiter.
        L = ceiling * std::tanh (L / ceiling);
        if (chR) R = ceiling * std::tanh (R / ceiling);

        chL[i] = (dryL * (1.0f - mix) + L * mix) * outG;
        if (chR) chR[i] = (dryR * (1.0f - mix) + R * mix) * outG;
    }

    outputMeter.store (buffer.getMagnitude (0, n));
    deEssMeter.store (grPeak);
    gateMeter.store (gatePeak);
}

void SattariVocalMasterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariVocalMasterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SattariVocalMasterAudioProcessor::createEditor()
{
    return new SattariVocalMasterAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariVocalMasterAudioProcessor();
}
