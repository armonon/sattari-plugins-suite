#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

namespace { const juce::StringArray kModes { "Shift", "Quantize", "Robot" };
           const juce::StringArray kNotes { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }

SattariMorphAudioProcessor::SattariMorphAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MORPH", createLayout()),
      presetBank (apvts, "morph")
{
    for (auto& b : busScale_) b = true;
    registerFactoryPresets();
}

// Deterministic "recipe" presets — the fast way to a usable transform. Values are
// raw parameter values (mode/robotnote are choice indices). mix=1 keeps them fully
// wet; the dry-delay path means Mix is always safe to pull back.
void SattariMorphAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // Every preset sets EVERY parameter, so recalling one is deterministic — otherwise a
    // preset would inherit whatever Key Echo / Harmony Voice happened to be dialled in.
    //   mode 0 Shift/1 Quantize/2 Robot | robotnote 0..11 (9=A)
    //   echomode + harmvoice: 0 Unison|Off, 1 Oct up, 2 Oct down, 3 Fifth, 4 Diatonic 3rd
    auto P = [] (float mode, float pitch, float formant, float robotnote, float drive, float mix,
                 float echomode, float echotime, float echofb, float echomix,
                 float harmvoice, float harmmix) -> PV
    {
        return PV { {"mode",mode}, {"pitch",pitch}, {"formant",formant}, {"robotnote",robotnote},
                    {"drive",drive}, {"mix",mix}, {"echomode",echomode}, {"echotime",echotime},
                    {"echofeedback",echofb}, {"echomix",echomix},
                    {"harmvoice",harmvoice}, {"harmmix",harmmix} };
    };
    //                                          mode pitch form robot drive  mix | ecmode ectime ecfb ecmix | hvoice hmix
    presetBank.addFactoryPreset ("Init",              P (0,   0,   0,   9, 0.00f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Chipmunk",          P (0,  12,   4,   9, 0.00f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Deep Demon",        P (0, -12,  -5,   9, 0.15f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Octave Up",         P (0,  12,   0,   9, 0.00f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Octave Down",       P (0, -12,   0,   9, 0.00f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Femme \xe2\x86\x92 Masc", P (0, -2, -6, 9, 0.00f, 1.0f,  1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Masc \xe2\x86\x92 Femme", P (0,  2,  6, 9, 0.00f, 1.0f,  1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Telephone",         P (0,   0,   3,   9, 0.40f, 0.9f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Alien",             P (0,   7,  -8,   9, 0.20f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("Robot (key-lock)",  P (1,   0,   0,   9, 0.00f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    presetBank.addFactoryPreset ("T-Pain Monotone",   P (2,   0,   0,   9, 0.10f, 1.0f,   1, 300, 0.35f, 0.00f,   0, 0.00f));
    // Showcase the in-key features so they're discoverable from the preset menu.
    presetBank.addFactoryPreset ("Shimmer Echo",      P (0,   0,   0,   9, 0.00f, 1.0f,   1, 350, 0.50f, 0.45f,   0, 0.00f));
    presetBank.addFactoryPreset ("Dub Throw",         P (0,   0,   0,   9, 0.10f, 1.0f,   2, 500, 0.60f, 0.50f,   0, 0.00f));
    presetBank.addFactoryPreset ("Harmonised Lead",   P (0,   0,   0,   9, 0.00f, 1.0f,   1, 300, 0.35f, 0.00f,   4, 0.45f));
    presetBank.addFactoryPreset ("Fifth Stack",       P (0,   0,   0,   9, 0.00f, 1.0f,   1, 300, 0.35f, 0.00f,   3, 0.40f));
    presetBank.addFactoryPreset ("Choir in Key",      P (0,   0,   0,   9, 0.00f, 1.0f,   4, 420, 0.45f, 0.35f,   4, 0.35f));
}

int SattariMorphAudioProcessor::getNumPrograms()    { return juce::jmax (1, presetBank.numFactoryPresets()); }
int SattariMorphAudioProcessor::getCurrentProgram() { return currentProgram; }

void SattariMorphAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= presetBank.numFactoryPresets()) return;
    currentProgram = index;
    presetBank.loadFactoryPreset (index);
}

const juce::String SattariMorphAudioProcessor::getProgramName (int index)
{
    const auto names = presetBank.factoryPresetNames();
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariMorphAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterChoice> ("mode", "Mode", kModes, 0));
    p.push_back (std::make_unique<AudioParameterFloat>  ("pitch", "Pitch",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("formant", "Formant",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterChoice> ("robotnote", "Robot Note", kNotes, 9));
    p.push_back (std::make_unique<AudioParameterFloat>  ("drive", "Drive",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("mix", "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f));
    // Key Echo — a pitched feedback delay whose repeats are harmonised in key. The
    // harmony is the CURRENT (pitched) input shifted by an in-key interval, then delayed,
    // so PSOLA always runs on a valid period. echomix defaults to 0 (echo off).
    p.push_back (std::make_unique<AudioParameterChoice> ("echomode", "Echo",
        juce::StringArray { "Unison", "Octave Up", "Octave Down", "Fifth", "Diatonic 3rd" }, 1));
    p.push_back (std::make_unique<AudioParameterFloat>  ("echotime", "Echo Time",
        NormalisableRange<float> (40.0f, 1000.0f, 1.0f), 300.0f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("echofeedback", "Echo Feedback",
        NormalisableRange<float> (0.0f, 0.9f, 0.01f), 0.35f));
    p.push_back (std::make_unique<AudioParameterFloat>  ("echomix", "Echo Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    // Harmony voice — a dry, in-key parallel voice (same interval engine as the echo,
    // but immediate, not delayed). Off + 0 mix by default, so existing patches are unchanged.
    p.push_back (std::make_unique<AudioParameterChoice> ("harmvoice", "Harmony",
        juce::StringArray { "Off", "Octave Up", "Octave Down", "Fifth", "Diatonic 3rd" }, 0));
    p.push_back (std::make_unique<AudioParameterFloat>  ("harmmix", "Harmony Level",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    return { p.begin(), p.end() };
}

void SattariMorphAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate;
    pitch_.prepare (sampleRate, 60.0f, 1000.0f, 0.045f);
    psola_.prepare (sampleRate, 60.0, 1000.0);
    formant_.prepare (sampleRate, 10, 4);
    const int n = juce::jmax (32, samplesPerBlock);
    dryMono_.assign ((size_t) n, 0.0f);
    wet_.assign ((size_t) n, 0.0f);
    totalLatency_ = psola_.getLatencySamples() + formant_.latencySamples();
    dryDelay_.assign ((size_t) (totalLatency_ + n + 4), 0.0f);
    dryPos_ = 0; lastPeriod_ = sampleRate / 220.0; lastScale_ = 1.0f;
    setLatencySamples (totalLatency_);

    // Key Echo scratch/state: max echo time is 1 s, allocation-free thereafter.
    echoShifter_.prepare (sampleRate, 60.0, 1000.0);
    maxEchoDelay_ = (int) (sampleRate * 1.0) + 1;
    echoBuf_.assign ((size_t) (maxEchoDelay_ + n + 4), 0.0f);
    echoHarm_.assign ((size_t) n, 0.0f);
    out_.assign ((size_t) n, 0.0f);
    echoPos_ = 0;

    harmShifter_.prepare (sampleRate, 60.0, 1000.0);
    harmScratch_.assign ((size_t) n, 0.0f);
}

void SattariMorphAudioProcessor::reset()
{
    std::fill (dryMono_.begin(),    dryMono_.end(),    0.0f);
    std::fill (wet_.begin(),        wet_.end(),        0.0f);
    std::fill (dryDelay_.begin(),   dryDelay_.end(),   0.0f);
    std::fill (out_.begin(),        out_.end(),        0.0f);
    std::fill (echoBuf_.begin(),    echoBuf_.end(),    0.0f);
    std::fill (echoHarm_.begin(),   echoHarm_.end(),   0.0f);
    std::fill (harmScratch_.begin(),harmScratch_.end(),0.0f);
    dryPos_ = 0; echoPos_ = 0;
    lastPeriod_ = sr_ / 220.0; lastScale_ = 1.0f;
    pitch_.reset(); psola_.reset(); formant_.reset();
    echoShifter_.reset(); harmShifter_.reset();
}

bool SattariMorphAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto o = layouts.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void SattariMorphAudioProcessor::pullBusScale()
{
    if (auto m = sattari::xplugin::readSession (30))
    {
        static const int MAJ[] = { 0,2,4,5,7,9,11 }, MIN[] = { 0,2,3,5,7,8,10 };
        for (auto& b : busScale_) b = false;
        if (m->scale == 2) { for (auto& b : busScale_) b = true; }
        else { const int* s = (m->scale == 1) ? MIN : MAJ; for (int i = 0; i < 7; ++i) busScale_[(size_t) ((m->root + s[i]) % 12)] = true; }
    }
}

void SattariMorphAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    auto main = getBusBuffer (buffer, true, 0);
    const int nch = main.getNumChannels();
    if ((int) dryMono_.size() < n) { dryMono_.assign ((size_t) n, 0.0f); wet_.assign ((size_t) n, 0.0f); }

    const int mode = (int) apvts.getRawParameterValue ("mode")->load();   // 0 Shift 1 Quantize 2 Robot
    const float pitchKnob = apvts.getRawParameterValue ("pitch")->load();
    const float formantSt = apvts.getRawParameterValue ("formant")->load();
    const int   robotNote = (int) apvts.getRawParameterValue ("robotnote")->load();
    const float drive = apvts.getRawParameterValue ("drive")->load();
    const float mix   = apvts.getRawParameterValue ("mix")->load();

    if (mode == 1)   // Quantize needs the bus key (poll off the fast path)
    {
        const auto now = juce::Time::currentTimeMillis();
        if (now - lastBusPollMs_ > 500) { lastBusPollMs_ = now; pullBusScale(); }
    }

    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f; for (int c = 0; c < nch; ++c) s += main.getSample (c, i);
        dryMono_[(size_t) i] = nch > 0 ? s / nch : 0.0f;
    }

    // Detect pitch for Quantize/Robot + the PSOLA period.
    pitch_.pushBlock (dryMono_.data(), n);
    const auto pr = pitch_.analyse (0.2f);
    double period = lastPeriod_;
    if (pr.voiced && pr.frequencyHz > 0.0f) { period = sr_ / pr.frequencyHz; lastPeriod_ = period; }

    // Pitch-shift ratio by mode. The Pitch knob transposes in every mode.
    float scale = std::pow (2.0f, pitchKnob / 12.0f);
    if ((mode == 1 || mode == 2) && pr.voiced && pr.frequencyHz > 0.0f)
    {
        const float detMidi = 69.0f + 12.0f * std::log2 (pr.frequencyHz / 440.0f);
        float tgtMidi;
        if (mode == 1)
        {
            int best = (int) std::lround (detMidi); float bd = 1e9f;
            for (int d = -7; d <= 7; ++d) { const int mm = (int) std::lround (detMidi) + d; if (! busScale_[(size_t) (((mm % 12) + 12) % 12)]) continue; const float dist = std::abs (detMidi - mm); if (dist < bd) { bd = dist; best = mm; } }
            tgtMidi = (float) best;
        }
        else   // Robot: nearest octave of the fixed note
            tgtMidi = std::round ((detMidi - robotNote) / 12.0f) * 12.0f + robotNote;
        scale *= std::pow (2.0f, (tgtMidi - detMidi) / 12.0f);
    }
    scale = juce::jlimit (0.25f, 4.0f, scale);
    lastScale_ = scale;

    // 1) pitch shift (TD-PSOLA), 2) formant warp, 3) drive.
    psola_.process (dryMono_.data(), wet_.data(), n, scale, period);
    sattari::dsp::FormantShifter::Params fp; fp.shiftSemitones = formantSt; fp.amount = 1.0f;
    formant_.process (wet_.data(), wet_.data(), n, fp);
    if (drive > 0.0001f)
    {
        const float g = 1.0f + drive * 6.0f, mk = 1.0f / std::tanh (g);
        for (int i = 0; i < n; ++i) wet_[(size_t) i] = std::tanh (wet_[(size_t) i] * g) * mk;
    }

    // Delay dry to the wet latency, then mix -> mono out scratch.
    if ((int) out_.size() < n) out_.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        dryDelay_[(size_t) dryPos_] = dryMono_[(size_t) i];
        const int rd = (dryPos_ - totalLatency_ + (int) dryDelay_.size()) % (int) dryDelay_.size();
        const float dry = dryDelay_[(size_t) rd];
        dryPos_ = (dryPos_ + 1) % (int) dryDelay_.size();
        out_[(size_t) i] = mix * wet_[(size_t) i] + (1.0f - mix) * dry;
    }

    // Harmony voice — a dry, in-key parallel voice of the current input (immediate, no
    // delay). Same interval engine as the echo. Off / level 0 => silent.
    const int   harmVoice = (int) apvts.getRawParameterValue ("harmvoice")->load();
    const float harmMix   = apvts.getRawParameterValue ("harmmix")->load();
    if (harmVoice > 0 && harmMix > 0.0001f)
    {
        if (harmVoice == 4)
        {
            const auto now = juce::Time::currentTimeMillis();
            if (now - lastBusPollMs_ > 500) { lastBusPollMs_ = now; pullBusScale(); }
        }
        const float hr = echoHarmonyRatio (harmVoice, pr.voiced, pr.frequencyHz);
        if ((int) harmScratch_.size() < n) harmScratch_.assign ((size_t) n, 0.0f);
        harmShifter_.process (dryMono_.data(), harmScratch_.data(), n, hr, period);
        for (int i = 0; i < n; ++i) out_[(size_t) i] += harmMix * harmScratch_[(size_t) i];
    }

    // Key Echo: harmonise the CURRENT (pitched) input in key, feed it a feedback delay,
    // and add the delayed harmony back — so the repeats are in-key. Bypassed at mix 0.
    const float echoMix = apvts.getRawParameterValue ("echomix")->load();
    if (echoMix > 0.0001f && ! echoBuf_.empty())
    {
        const int   echoMode = (int) apvts.getRawParameterValue ("echomode")->load();
        const float echoTime = apvts.getRawParameterValue ("echotime")->load();
        const float echoFb   = juce::jlimit (0.0f, 0.9f, apvts.getRawParameterValue ("echofeedback")->load());
        if (echoMode == 4)   // Diatonic 3rd needs the session key
        {
            const auto now = juce::Time::currentTimeMillis();
            if (now - lastBusPollMs_ > 500) { lastBusPollMs_ = now; pullBusScale(); }
        }
        const float hratio = echoHarmonyRatio (echoMode, pr.voiced, pr.frequencyHz);
        if ((int) echoHarm_.size() < n) echoHarm_.assign ((size_t) n, 0.0f);
        echoShifter_.process (dryMono_.data(), echoHarm_.data(), n, hratio, period);

        const int bsz = (int) echoBuf_.size();
        const int delaySamp = juce::jlimit (1, maxEchoDelay_, (int) (echoTime * sr_ / 1000.0));
        for (int i = 0; i < n; ++i)
        {
            const int rdd = (echoPos_ - delaySamp + bsz) % bsz;
            const float delayed = echoBuf_[(size_t) rdd];
            echoBuf_[(size_t) echoPos_] = echoHarm_[(size_t) i] + echoFb * delayed;
            echoPos_ = (echoPos_ + 1) % bsz;
            out_[(size_t) i] += echoMix * delayed;
        }
    }

    for (int i = 0; i < n; ++i)
        for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.setSample (c, i, out_[(size_t) i]);
}

// In-key echo interval as a pitch ratio. Octaves/fifth need no key; the diatonic third
// walks two scale degrees up from the current note using the session-bus scale (falling
// back to a major third when there's no key or the note is unvoiced).
float SattariMorphAudioProcessor::echoHarmonyRatio (int mode, bool voiced, float freqHz) const
{
    switch (mode)
    {
        case 1: return 2.0f;                              // Octave up
        case 2: return 0.5f;                              // Octave down
        case 3: return std::pow (2.0f, 7.0f / 12.0f);     // Fifth
        case 4:                                           // Diatonic 3rd (key-aware)
        {
            if (! voiced || freqHz <= 0.0f) return std::pow (2.0f, 4.0f / 12.0f);
            const float detMidi = 69.0f + 12.0f * std::log2 (freqHz / 440.0f);
            int base = (int) std::lround (detMidi); float bd = 1.0e9f;
            for (int d = -6; d <= 6; ++d)
            {
                const int m = (int) std::lround (detMidi) + d;
                if (! busScale_[(size_t) (((m % 12) + 12) % 12)]) continue;
                const float dist = std::abs (detMidi - (float) m);
                if (dist < bd) { bd = dist; base = m; }
            }
            // Two scale steps up, in key. With no session key the bus mask is all-true
            // (chromatic), and the shared walk falls back to a fixed major third rather
            // than degenerating into a whole tone.
            const int m = sattari::dsp::scaleStepsAbove (base, busScale_.data(), 2, 4);
            return std::pow (2.0f, ((float) m - detMidi) / 12.0f);
        }
        default: return 1.0f;                             // Unison
    }
}

void SattariMorphAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void SattariMorphAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
juce::AudioProcessorEditor* SattariMorphAudioProcessor::createEditor() { return new SattariMorphAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariMorphAudioProcessor(); }
