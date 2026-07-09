#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace { inline double clampF (double f, double sr) { return juce::jlimit (20.0, sr * 0.45, f); } }

// -- 2nd-order Butterworth biquads (RBJ, Q = 1/sqrt2). Two cascaded = LR4. -----
void SattariImageAudioProcessor::Biquad::setLP2 (double f, double sr) noexcept
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / (2.0 * 0.70710678118654752);
    const double a0 = 1.0 + alpha;
    b0 = ((1.0 - c) * 0.5) / a0; b1 = (1.0 - c) / a0; b2 = ((1.0 - c) * 0.5) / a0;
    a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
}

void SattariImageAudioProcessor::Biquad::setHP2 (double f, double sr) noexcept
{
    const double w0 = 2.0 * juce::MathConstants<double>::pi * clampF (f, sr) / sr;
    const double c = std::cos (w0), s = std::sin (w0);
    const double alpha = s / (2.0 * 0.70710678118654752);
    const double a0 = 1.0 + alpha;
    b0 = ((1.0 + c) * 0.5) / a0; b1 = (-(1.0 + c)) / a0; b2 = ((1.0 + c) * 0.5) / a0;
    a1 = (-2.0 * c) / a0; a2 = (1.0 - alpha) / a0;
}

// -- Processor ----------------------------------------------------------------
SattariImageAudioProcessor::SattariImageAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      presetBank (apvts, "imager")
{
    registerFactoryPresets();
}

void SattariImageAudioProcessor::registerFactoryPresets()
{
    presetBank.addFactoryPreset ("Init / Neutral",
        { {"width", 1.0f}, {"widthLow", 1.0f}, {"widthMid", 1.0f}, {"widthHigh", 1.0f},
          {"lowMono", 0.0f}, {"lowMonoFreq", 120.0f}, {"haas", 0.0f}, {"monoAudition", 0.0f} });
    presetBank.addFactoryPreset ("Wide & Safe",
        { {"width", 1.15f}, {"widthLow", 0.5f}, {"widthMid", 1.2f}, {"widthHigh", 1.6f},
          {"lowMono", 1.0f}, {"lowMonoFreq", 120.0f}, {"haas", 0.0f} });
    presetBank.addFactoryPreset ("Bass Mono",
        { {"width", 1.0f}, {"widthLow", 0.0f}, {"widthMid", 1.0f}, {"widthHigh", 1.1f},
          {"lowMono", 1.0f}, {"lowMonoFreq", 160.0f}, {"haas", 0.0f} });
    presetBank.addFactoryPreset ("Mono Collapse",
        { {"width", 0.0f}, {"widthLow", 1.0f}, {"widthMid", 1.0f}, {"widthHigh", 1.0f},
          {"lowMono", 0.0f}, {"haas", 0.0f} });
    presetBank.addFactoryPreset ("Air Spread",
        { {"width", 1.1f}, {"widthLow", 0.6f}, {"widthMid", 1.1f}, {"widthHigh", 1.8f},
          {"lowMono", 1.0f}, {"lowMonoFreq", 110.0f}, {"haas", 0.35f} });
}

int SattariImageAudioProcessor::getNumPrograms()    { return juce::jmax (1, presetBank.numFactoryPresets()); }
int SattariImageAudioProcessor::getCurrentProgram() { return currentProgram; }

void SattariImageAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetBank.numFactoryPresets())
        return;
    currentProgram = index;
    presetBank.loadFactoryPreset (index);
}

const juce::String SattariImageAudioProcessor::getProgramName (int index)
{
    const auto names = presetBank.factoryPresetNames();
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariImageAudioProcessor::createParameterLayout()
{
    using FP = juce::AudioParameterFloat;
    using R  = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // Widths: 0 = mono (no side), 1 = neutral, 2 = double the stereo spread.
    p.push_back (std::make_unique<FP> (juce::ParameterID ("width",     1), "Width",      R (0.0f, 2.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<FP> (juce::ParameterID ("widthLow",  1), "Low Width",  R (0.0f, 2.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<FP> (juce::ParameterID ("widthMid",  1), "Mid Width",  R (0.0f, 2.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<FP> (juce::ParameterID ("widthHigh", 1), "High Width", R (0.0f, 2.0f, 0.01f), 1.0f));

    // Band crossovers for the per-band side widths.
    p.push_back (std::make_unique<FP> (juce::ParameterID ("xoverLoMid", 1), "Low/Mid Xover",
        R (60.0f, 800.0f, 1.0f, 0.4f), 250.0f));
    p.push_back (std::make_unique<FP> (juce::ParameterID ("xoverMidHi", 1), "Mid/High Xover",
        R (1200.0f, 12000.0f, 1.0f, 0.4f), 4000.0f));

    // Elliptical low-mono: side removed below this freq. Toggle + frequency.
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("lowMono", 1), "Low Mono", false));
    p.push_back (std::make_unique<FP> (juce::ParameterID ("lowMonoFreq", 1), "Low Mono Freq",
        R (40.0f, 400.0f, 1.0f, 0.4f), 120.0f));

    // Haas/ensemble spread (0 = off, applied to the widened side).
    p.push_back (std::make_unique<FP> (juce::ParameterID ("haas", 1), "Haas / Ensemble", R (0.0f, 1.0f, 0.01f), 0.0f));

    // Mono-compat audition (sums to mono; does not alter automation of the rest).
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("monoAudition", 1), "Mono Audition", false));

    return { p.begin(), p.end() };
}

void SattariImageAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    sideLowLP.reset();
    sideHighHP.reset();
    lowMonoHP.reset();

    // Haas delay ring: up to 20 ms, preallocated (no alloc in processBlock).
    haasCap = juce::jmax (1, (int) std::ceil (sampleRate * 0.02) + 1);
    haasRing.assign ((size_t) haasCap, 0.0f);
    haasWrite = 0;

    corrLL = corrRR = corrLR = 0.0; corrCount = 0;
    corr.store (0.0f, std::memory_order_relaxed);
}

bool SattariImageAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SattariImageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // MIDI Learn: mapped CCs steer params (armed param binds to next CC seen).
    midiLearn.processMidi (midi);

    const int n     = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (n <= 0 || numCh <= 0)
        return;

    // Mono in/out: nothing to image. Still keep the correlation readout sane.
    if (numCh < 2)
    {
        corr.store (1.0f, std::memory_order_relaxed);
        return;
    }

    const float width     = param ("width");
    const float widthLow  = param ("widthLow");
    const float widthMid  = param ("widthMid");
    const float widthHigh = param ("widthHigh");
    const float loMidHz   = param ("xoverLoMid");
    const float midHiHz   = param ("xoverMidHi");
    const bool  lowMono   = param ("lowMono") > 0.5f;
    const float lowMonoHz = param ("lowMonoFreq");
    const float haasAmt   = juce::jlimit (0.0f, 1.0f, param ("haas"));
    const bool  monoAud   = param ("monoAudition") > 0.5f;

    // Per-block coefficients. LR4 = two cascaded 2nd-order Butterworths.
    sideLowLP.a.setLP2 (loMidHz, sampleRate); sideLowLP.b.setLP2 (loMidHz, sampleRate);
    sideHighHP.a.setHP2 (midHiHz, sampleRate); sideHighHP.b.setHP2 (midHiHz, sampleRate);
    lowMonoHP.a.setHP2 (lowMonoHz, sampleRate); lowMonoHP.b.setHP2 (lowMonoHz, sampleRate);

    // Fast neutral-detection so width=1 everywhere really is bit-exact: if every
    // width is exactly 1, low-mono off, no Haas, no mono-audition, skip all the
    // side filtering (which would otherwise add tiny FP rounding).
    const bool neutral = (width == 1.0f && widthLow == 1.0f && widthMid == 1.0f
                          && widthHigh == 1.0f && ! lowMono && haasAmt <= 0.0f && ! monoAud);

    // Haas delay length scales with the amount (0..~12 ms).
    const int haasDelay = juce::jlimit (0, haasCap - 1,
        (int) std::lround ((double) haasAmt * sampleRate * 0.012));

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    const int corrPeriod = juce::jmax (1, (int) (sampleRate * 0.3));

    for (int i = 0; i < n; ++i)
    {
        const float l = L[i];
        const float r = R[i];

        // -- M/S encode. Mid is NEVER modified -> mono sum is preserved. -------
        const float mid  = 0.5f * (l + r);
        float       side = 0.5f * (l - r);

        if (! neutral)
        {
            // -- 3-band side split via complementary LR4 crossovers. ----------
            // low + mid + high == side exactly (mid defined by subtraction).
            const double low  = sideLowLP.process ((double) side);
            const double high = sideHighHP.process ((double) side);
            const double midB = (double) side - low - high;
            side = (float) (low * (double) widthLow
                          + midB * (double) widthMid
                          + high * (double) widthHigh);

            // -- Global width on top. -----------------------------------------
            side *= width;

            // -- Elliptical low-mono: strip the side below the cutoff. ---------
            if (lowMono)
                side = (float) lowMonoHP.process ((double) side);

            // -- Haas / ensemble: blend in a short-delayed copy of the side. --
            const float delayed = haasRing[(size_t) ((haasWrite - haasDelay + haasCap) % haasCap)];
            haasRing[(size_t) haasWrite] = side;
            if (++haasWrite >= haasCap) haasWrite = 0;
            if (haasAmt > 0.0f)
                side = side + haasAmt * (delayed - side) * 0.5f;
        }

        // -- M/S decode back to L/R. ------------------------------------------
        float outL = mid + side;
        float outR = mid - side;

        // -- Mono-compat audition: sum to mono for checking. ------------------
        if (monoAud)
        {
            outL = mid;   // side == 0 -> pure mono, level-matched to the sum
            outR = mid;
        }

        // Safety clamp (never let a pathological state blow up the output).
        outL = juce::jlimit (-4.0f, 4.0f, outL);
        outR = juce::jlimit (-4.0f, 4.0f, outR);

        L[i] = outL;
        R[i] = outR;

        // -- Correlation meter (mirrors plugins/meter). -----------------------
        corrLL += (double) outL * outL;
        corrRR += (double) outR * outR;
        corrLR += (double) outL * outR;
        if (++corrCount >= corrPeriod)
        {
            const double den = std::sqrt (corrLL * corrRR);
            corr.store (den > 1.0e-12 ? (float) juce::jlimit (-1.0, 1.0, corrLR / den) : 1.0f,
                        std::memory_order_relaxed);
            corrLL = corrRR = corrLR = 0.0; corrCount = 0;
        }
    }
}

void SattariImageAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariImageAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* SattariImageAudioProcessor::createEditor()
{
    return new SattariImageAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariImageAudioProcessor();
}
