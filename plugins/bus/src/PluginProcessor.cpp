#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::StringArray modeChoices { "Clean", "Glue", "Console", "Tape" };

float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -120.0f); }
}

// ---- Biquad coefficient setters (RBJ cookbook) ------------------------------
void SattariBusAudioProcessor::Biquad::setPeak (double fs, double f, double q, double gainDb) noexcept
{
    const double A   = std::pow (10.0, gainDb / 40.0);
    const double w0  = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
    const double cw  = std::cos (w0);
    const double sw  = std::sin (w0);
    const double alpha = sw / (2.0 * juce::jmax (0.05, q));
    const double a0 = 1.0 + alpha / A;
    b0 = (float) ((1.0 + alpha * A) / a0);
    b1 = (float) ((-2.0 * cw) / a0);
    b2 = (float) ((1.0 - alpha * A) / a0);
    a1 = (float) ((-2.0 * cw) / a0);
    a2 = (float) ((1.0 - alpha / A) / a0);
}

void SattariBusAudioProcessor::Biquad::setLowShelf (double fs, double f, double gainDb) noexcept
{
    const double A   = std::pow (10.0, gainDb / 40.0);
    const double w0  = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
    const double cw  = std::cos (w0);
    const double sw  = std::sin (w0);
    const double alpha = sw / (2.0 * 0.707);
    const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha;
    b0 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha)) / a0);
    b1 = (float) ((2.0 * A * ((A - 1.0) - (A + 1.0) * cw)) / a0);
    b2 = (float) ((A * ((A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha)) / a0);
    a1 = (float) ((-2.0 * ((A - 1.0) + (A + 1.0) * cw)) / a0);
    a2 = (float) (((A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha) / a0);
}

void SattariBusAudioProcessor::Biquad::setHighShelf (double fs, double f, double gainDb) noexcept
{
    const double A   = std::pow (10.0, gainDb / 40.0);
    const double w0  = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
    const double cw  = std::cos (w0);
    const double sw  = std::sin (w0);
    const double alpha = sw / (2.0 * 0.707);
    const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha;
    b0 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha)) / a0);
    b1 = (float) ((-2.0 * A * ((A - 1.0) + (A + 1.0) * cw)) / a0);
    b2 = (float) ((A * ((A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha)) / a0);
    a1 = (float) ((2.0 * ((A - 1.0) - (A + 1.0) * cw)) / a0);
    a2 = (float) (((A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha) / a0);
}

// ---- Processor --------------------------------------------------------------
SattariBusAudioProcessor::SattariBusAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetBank (apvts, "bus")
{
    registerFactoryPresets();
}

// Factory presets exposed as host programs (Logic/Live/etc. preset menu). Each is
// a sparse paramID -> value map; unlisted params keep their default.
void SattariBusAudioProcessor::registerFactoryPresets()
{
    presetBank.addFactoryPreset ("Init / Clean",
        { {"mode", 0.0f}, {"drive", 0.0f}, {"mix", 100.0f}, {"width", 1.0f}, {"pan", 0.0f},
          {"low", 0.0f}, {"mid", 0.0f}, {"high", 0.0f}, {"makeup", 0.0f}, {"output", 0.0f} });
    presetBank.addFactoryPreset ("Mix Glue",
        { {"mode", 1.0f}, {"threshold", -22.0f}, {"ratio", 2.5f}, {"attack", 20.0f},
          {"release", 150.0f}, {"makeup", 2.0f}, {"drive", 0.12f}, {"low", 1.0f},
          {"high", 1.5f}, {"mix", 100.0f}, {"width", 1.0f} });
    presetBank.addFactoryPreset ("Drum Bus",
        { {"mode", 2.0f}, {"threshold", -16.0f}, {"ratio", 4.0f}, {"attack", 8.0f},
          {"release", 120.0f}, {"makeup", 1.5f}, {"drive", 0.25f}, {"low", 2.0f},
          {"high", 2.0f}, {"width", 1.1f}, {"mix", 100.0f} });
    presetBank.addFactoryPreset ("Master Polish",
        { {"mode", 3.0f}, {"threshold", -10.0f}, {"ratio", 1.8f}, {"attack", 30.0f},
          {"release", 250.0f}, {"makeup", 1.0f}, {"drive", 0.18f}, {"high", 1.0f},
          {"width", 1.05f}, {"ceiling", -0.3f}, {"mix", 100.0f} });
    presetBank.addFactoryPreset ("Parallel Crush",
        { {"mode", 2.0f}, {"threshold", -30.0f}, {"ratio", 8.0f}, {"attack", 2.0f},
          {"release", 80.0f}, {"makeup", 0.0f}, {"drive", 0.4f}, {"mix", 40.0f} });
}

int SattariBusAudioProcessor::getNumPrograms()            { return juce::jmax (1, presetBank.numFactoryPresets()); }
int SattariBusAudioProcessor::getCurrentProgram()         { return currentProgram; }

void SattariBusAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetBank.numFactoryPresets())
        return;
    currentProgram = index;
    presetBank.loadFactoryPreset (index);
}

const juce::String SattariBusAudioProcessor::getProgramName (int index)
{
    const auto names = presetBank.factoryPresetNames();
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariBusAudioProcessor::createParameterLayout()
{
    using P  = juce::AudioParameterFloat;
    using R  = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID ("mode", 1), "Mode", modeChoices, 1));
    p.push_back (std::make_unique<P> (juce::ParameterID ("inputTrim", 1), "Input Trim", R (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("hpf", 1), "HPF", R (20.0f, 400.0f, 1.0f, 0.4f), 20.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("lpf", 1), "LPF", R (2000.0f, 20000.0f, 1.0f, 0.4f), 20000.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("low", 1), "Low", R (-15.0f, 15.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("mid", 1), "Mid", R (-15.0f, 15.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("high", 1), "High", R (-15.0f, 15.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("threshold", 1), "Comp Threshold", R (-48.0f, 0.0f, 0.1f), -18.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("ratio", 1), "Comp Ratio", R (1.0f, 10.0f, 0.1f), 2.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("attack", 1), "Comp Attack", R (0.1f, 100.0f, 0.1f, 0.5f), 12.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("release", 1), "Comp Release", R (20.0f, 1000.0f, 1.0f, 0.5f), 180.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("makeup", 1), "Makeup", R (-12.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("drive", 1), "Drive", R (0.0f, 1.0f, 0.01f), 0.12f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("width", 1), "Width", R (0.0f, 2.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("pan", 1), "Pan", R (-1.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("mix", 1), "Mix", R (0.0f, 100.0f, 0.1f), 100.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("output", 1), "Output", R (-24.0f, 12.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<P> (juce::ParameterID ("ceiling", 1), "Ceiling", R (-12.0f, 0.0f, 0.1f), -0.3f));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("mute", 1), "Mute", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("bypass", 1), "Bypass", false));
    // M/S mode: encode L/R -> Mid + Side before EQ/comp/drive, decode back at
    // the end. Lets you compress the centre image without squashing the stereo
    // information, or shape the sides independently. The existing width knob
    // continues to scale the side signal (post-M/S in this mode) the same way.
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("msMode", 1), "M/S Process", false));

    return { p.begin(), p.end() };
}

float SattariBusAudioProcessor::getParam (const char* id) const noexcept
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void SattariBusAudioProcessor::setOnePoleLP (OnePole& pole, float cutoffHz) noexcept
{
    const float fc = juce::jlimit (10.0f, (float) (sampleRate * 0.49), cutoffHz);
    pole.a = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * fc / (float) sampleRate);
}

void SattariBusAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        hpf[(size_t) ch].reset();
        lpf[(size_t) ch].reset();
        lowShelf[(size_t) ch].reset();
        midBell[(size_t) ch].reset();
        highShelf[(size_t) ch].reset();
    }
    compFollower.prepare (sampleRate);
    grSmoother.prepare (sampleRate);
    smoothedGrDb = 0.0f;
    gainReductionDb.store (0.0f, std::memory_order_relaxed);

    // 4x oversampler for the drive stage (allocation-free after this).
    osChannels = juce::jlimit (1, 2, juce::jmax (1, getTotalNumOutputChannels()));
    const int block = juce::jmax (1, samplesPerBlock);
    // useIntegerLatency=true so the reported latency is a whole number of samples
    // and the dry parallel-mix ring lines up exactly (no fractional-sample skew).
    driveOS = std::make_unique<juce::dsp::Oversampling<float>> (
                  (size_t) osChannels, 2,
                  juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    driveOS->initProcessing ((size_t) block);
    driveOS->reset();

    driveDryStash.setSize (osChannels, block);
    driveDryStash.clear();
    driveDryRingLen = juce::jmax (1, (int) std::lround (driveOS->getLatencyInSamples()));
    driveDryRing.setSize (osChannels, driveDryRingLen);
    driveDryRing.clear();
    driveDryRingPos = 0;

    // Report the oversampler latency so the host delay-compensates the bus.
    setLatencySamples (driveDryRingLen);
}

bool SattariBusAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void SattariBusAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI Learn: feed any CCs to the learner. Steers mapped params; if the
    // user has armed a param, the next CC seen here binds to it.
    midiLearn.processMidi (midi);

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (getParam ("bypass") > 0.5f)
        return; // leave input untouched

    const int   mode      = (int) std::round (getParam ("mode"));
    const float inGain    = dbToGain (getParam ("inputTrim"));
    const float hpfHz     = getParam ("hpf");
    const float lpfHz     = getParam ("lpf");
    const float lowDb     = getParam ("low");
    const float midDb     = getParam ("mid");
    const float highDb    = getParam ("high");
    const float threshold = getParam ("threshold");
    const float ratio     = juce::jmax (1.0f, getParam ("ratio"));
    float       attackMs  = getParam ("attack");
    float       releaseMs = getParam ("release");
    const float makeup    = dbToGain (getParam ("makeup"));
    float       drive     = getParam ("drive");
    const float width     = getParam ("width");
    const float pan       = getParam ("pan");
    const float mix       = getParam ("mix") * 0.01f;
    const float outGain   = dbToGain (getParam ("output"));
    const float ceilLin   = dbToGain (getParam ("ceiling"));
    const bool  muted     = getParam ("mute") > 0.5f;

    // Character modes nudge the comp + drive feel.
    if (mode == 1) { drive += 0.05f; releaseMs *= 0.9f; }                 // Glue
    if (mode == 2) { drive += 0.12f; attackMs *= 0.8f; }                  // Console
    if (mode == 3) { drive += 0.22f; releaseMs *= 1.2f; }                 // Tape
    drive = juce::jlimit (0.0f, 1.3f, drive);

    // Per-block coefficients.
    setOnePoleLP (hpf[0], hpfHz); setOnePoleLP (hpf[1], hpfHz);
    setOnePoleLP (lpf[0], lpfHz); setOnePoleLP (lpf[1], lpfHz);
    for (int ch = 0; ch < 2; ++ch)
    {
        lowShelf[(size_t) ch].setLowShelf (sampleRate, 120.0, lowDb);
        midBell[(size_t) ch].setPeak (sampleRate, 1000.0, 0.9, midDb);
        highShelf[(size_t) ch].setHighShelf (sampleRate, 8000.0, highDb);
    }

    compFollower.setTimes (attackMs, releaseMs);
    grSmoother.setTimes (attackMs, releaseMs);
    const float driveK       = 1.0f + drive * 4.0f;
    const float driveNorm    = std::tanh (driveK);
    // Dry/wet blend so drive=0 is bit-transparent (no tanh colour, no level jump);
    // higher drive both blends in more shaper AND hardens the curve via driveK.
    const float driveMix     = juce::jlimit (0.0f, 1.0f, drive);
    const float panAngle     = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    const float panL         = std::cos (panAngle) * juce::MathConstants<float>::sqrt2;
    const float panR         = std::sin (panAngle) * juce::MathConstants<float>::sqrt2;

    auto* left  = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    // M/S mode: stash the original L/R as the parallel-mix dry FIRST so the
    // mix-back stays in L/R, then encode L/R -> M/S in-place. The pre-drive
    // pipeline (trim/EQ/comp) and drive nonlinearity then operate on M and S
    // separately; post-drive width re-balances the side and decodes to L/R.
    const bool msMode = getParam ("msMode") > 0.5f && right != nullptr;
    if (msMode)
    {
        // Stash dry L/R, then encode M/S in place. The existing per-sample
        // stash loop below will overwrite the stash with already-encoded
        // values, so we set a flag to skip its stash.
        for (int i = 0; i < numSamples; ++i)
        {
            driveDryStash.setSample (0, i, left[i]);
            if (osChannels > 1) driveDryStash.setSample (1, i, right[i]);
            const float l = left[i];
            const float r = right[i];
            left[i]  = 0.5f * (l + r);
            right[i] = 0.5f * (l - r);
        }
    }

    float blockMaxGr = 0.0f;

    // ---- Pre-drive: trim -> HPF/LPF -> tone -> glue comp. Stash the original
    // input for the parallel mix (delayed later to match the oversampler).
    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = left[i];
        const float dryR = right != nullptr ? right[i] : left[i];
        // In M/S mode the dry stash already has the original L/R captured
        // before the encode; do not overwrite it here with the M/S samples.
        if (! msMode)
        {
            driveDryStash.setSample (0, i, dryL);
            if (osChannels > 1) driveDryStash.setSample (1, i, dryR);
        }

        float l = dryL * inGain;
        float r = dryR * inGain;

        l = hpf[0].hp (l); r = hpf[1].hp (r);
        l = lpf[0].lp (l); r = lpf[1].lp (r);
        l = lowShelf[0].process (l); r = lowShelf[1].process (r);
        l = midBell[0].process (l);  r = midBell[1].process (r);
        l = highShelf[0].process (l); r = highShelf[1].process (r);

        const float detector = juce::jmax (std::abs (l), std::abs (r));
        const float compEnv = compFollower.process (detector);
        const float envDb = juce::Decibels::gainToDecibels (compEnv + 1.0e-9f, -120.0f);
        const float overDb = juce::jmax (0.0f, envDb - threshold);
        const float targetGrDb = -overDb * (1.0f - 1.0f / ratio);
        smoothedGrDb = grSmoother.processFalling (targetGrDb);
        const float compGain = dbToGain (smoothedGrDb) * makeup;
        l *= compGain; r *= compGain;
        blockMaxGr = juce::jmax (blockMaxGr, -smoothedGrDb);

        left[i] = l;
        if (right != nullptr) right[i] = r;
    }

    // ---- Drive nonlinearity, 4x oversampled (only the shaper runs hot) ----
    auto applyDrive = [driveK, driveNorm, driveMix] (float* d, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            const float x = d[i];
            const float shaped = std::tanh (x * driveK) / driveNorm;
            d[i] = x + driveMix * (shaped - x);
        }
    };

    if (driveOS != nullptr && numChannels == osChannels)
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);
        auto up = driveOS->processSamplesUp (block);
        for (int c = 0; c < numChannels; ++c)
            applyDrive (up.getChannelPointer ((size_t) c), (int) up.getNumSamples());
        driveOS->processSamplesDown (block);
    }
    else
    {
        applyDrive (left, numSamples);
        if (right != nullptr) applyDrive (right, numSamples);
    }

    // ---- Post-drive: width -> pan -> parallel mix (dry delayed to match the
    // oversampler) -> output -> soft ceiling. ----
    int rp = driveDryRingPos;
    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right != nullptr ? right[i] : left[i];

        // Width + pan. In M/S mode `l`/`r` are already M/S, so width just scales
        // the side and we decode back to L/R after. Pan is suppressed in M/S
        // (it would tilt the mid-side balance asymmetrically, not pan an L/R
        // image). In normal stereo mode, do the existing M/S width + pan.
        if (right != nullptr)
        {
            if (msMode)
            {
                const float mid  = l;
                const float side = r * width;
                l = mid + side;
                r = mid - side;
            }
            else
            {
                const float mid  = 0.5f * (l + r);
                const float side = 0.5f * (l - r) * width;
                l = mid + side;
                r = mid - side;
                l *= panL;
                r *= panR;
            }
        }

        const float ddL = driveDryRing.getSample (0, rp);
        const float ddR = osChannels > 1 ? driveDryRing.getSample (1, rp) : ddL;
        driveDryRing.setSample (0, rp, driveDryStash.getSample (0, i));
        if (osChannels > 1) driveDryRing.setSample (1, rp, driveDryStash.getSample (1, i));
        if (++rp >= driveDryRingLen) rp = 0;

        l = ddL * (1.0f - mix) + l * mix;
        r = ddR * (1.0f - mix) + r * mix;

        l *= outGain; r *= outGain;
        l = ceilLin * std::tanh (l / juce::jmax (1.0e-6f, ceilLin));
        r = ceilLin * std::tanh (r / juce::jmax (1.0e-6f, ceilLin));

        if (muted) { l = 0.0f; r = 0.0f; }

        left[i] = l;
        if (right != nullptr) right[i] = r;
    }
    driveDryRingPos = rp;

    gainReductionDb.store (blockMaxGr, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SattariBusAudioProcessor::createEditor()
{
    return new SattariBusAudioProcessorEditor (*this);
}

void SattariBusAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariBusAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariBusAudioProcessor();
}
