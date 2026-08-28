#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdio>
#include <memory>

using Engine = sattari::dsp::SattariGranularEngine;
using EngineParams = Engine::EngineParams;
using GranSource = sattari::granulator::SourceSample;
using DspSource = sattari::dsp::SourceSample;
using Layer = SattariGranulatorAudioProcessor::Layer;

// =============================================================================
//  Construction / parameter layout
// =============================================================================
SattariGranulatorAudioProcessor::SattariGranulatorAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          // Optional discrete audio INPUT bus so a DAW can feed Live Capture.
          // Declared with isActiveByDefault = false so the plugin still loads as
          // a pure synth in hosts that never route audio in.
          .withInput  ("Capture In", juce::AudioChannelSet::stereo(), false)
          .withOutput ("Output",     juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    formats.registerBasicFormats();   // wav / aiff / flac / ogg / mp3 (platform dependent)
    registerFactoryPresets();
}

// Factory presets: real parameter values on Layer A (layerMorph defaults to 0 = A only)
// plus shared globals. Only listed params are set; anything omitted keeps its default,
// so presets stay valid as params evolve.
void SattariGranulatorAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    presetBank.addFactoryPreset ("Pad", PV{
        {"A_grainSize", 220.0f}, {"A_density", 40.0f}, {"A_densityMode", 1}, {"A_spray", 0.30f},
        {"A_smoothness", 0.60f}, {"A_window", 1}, {"A_grainPan", 0.40f},
        {"ampAttack", 400.0f}, {"ampDecay", 800.0f}, {"ampSustain", 1.0f}, {"ampRelease", 1500.0f},
        {"reverbMix", 0.35f}, {"reverbSize", 0.70f}, {"reverbDamp", 0.40f},
        {"width", 1.30f}, {"volume", -6.0f} });

    presetBank.addFactoryPreset ("Texture", PV{
        {"A_grainSize", 90.0f}, {"A_density", 30.0f}, {"A_densityMode", 1}, {"A_spray", 0.50f},
        {"A_smoothness", 0.40f}, {"A_window", 2}, {"A_grainPan", 0.60f}, {"A_posLfoAmt", 0.30f},
        {"ampAttack", 150.0f}, {"ampRelease", 800.0f},
        {"reverbMix", 0.25f}, {"reverbSize", 0.50f},
        {"width", 1.20f}, {"volume", -6.0f} });

    presetBank.addFactoryPreset ("Glitch", PV{
        {"A_grainSize", 20.0f}, {"A_density", 60.0f}, {"A_densityMode", 0}, {"A_spray", 0.80f},
        {"A_srcReverse", 1}, {"A_posScanRate", 0.50f}, {"A_grainPan", 0.80f}, {"A_smoothness", 0.0f},
        {"ampAttack", 5.0f}, {"ampDecay", 200.0f}, {"ampRelease", 200.0f},
        {"width", 1.0f}, {"volume", -6.0f} });

    presetBank.addFactoryPreset ("Ambient Cloud", PV{
        {"A_grainSize", 300.0f}, {"A_density", 15.0f}, {"A_densityMode", 1}, {"A_spray", 0.70f},
        {"A_smoothness", 0.80f}, {"A_window", 1},
        {"ampAttack", 800.0f}, {"ampSustain", 1.0f}, {"ampRelease", 3000.0f},
        {"pitchLfoAmt", 15.0f}, {"pitchLfoRate", 3.0f},
        {"reverbMix", 0.50f}, {"reverbSize", 0.85f}, {"reverbDamp", 0.30f},
        {"width", 1.40f}, {"volume", -8.0f} });

    presetBank.addFactoryPreset ("Rhythmic Stutter", PV{
        {"A_grainSize", 60.0f}, {"A_densitySync", 1}, {"A_densityDiv", 6}, {"A_densityMode", 0},
        {"A_spray", 0.20f}, {"A_grainPan", 0.50f},
        {"ampAttack", 5.0f}, {"ampDecay", 300.0f}, {"ampRelease", 150.0f},
        {"width", 1.10f}, {"volume", -6.0f} });

    presetBank.addFactoryPreset ("Formant Vox", PV{
        {"A_grainSize", 80.0f}, {"A_density", 25.0f}, {"A_densityMode", 1}, {"A_formantShift", 3.0f},
        {"A_unisonVoices", 2}, {"A_unisonDetune", 12.0f}, {"A_unisonWidth", 0.60f},
        {"filterType", 0}, {"cutoff", 4000.0f}, {"reso", 0.20f},
        {"ampAttack", 60.0f}, {"ampRelease", 500.0f},
        {"width", 1.20f}, {"volume", -6.0f} });

    presetBank.addFactoryPreset ("Deep Grain Bass", PV{
        {"A_grainSize", 120.0f}, {"A_density", 30.0f}, {"A_densityMode", 1}, {"A_tuneCoarse", -12},
        {"A_spray", 0.15f},
        {"subOsc", 1}, {"subLevel", 0.50f}, {"subWave", 0}, {"subOct", -1},
        {"filterType", 0}, {"cutoff", 2000.0f}, {"reso", 0.15f},
        {"ampAttack", 10.0f}, {"ampDecay", 500.0f}, {"ampRelease", 400.0f},
        {"voiceMode", 0}, {"width", 0.80f}, {"volume", -6.0f} });
}

SattariGranulatorAudioProcessor::~SattariGranulatorAudioProcessor() = default;   // the module stops its own bus readers






// -----------------------------------------------------------------------------
//  Per-layer parameter id table (base ids, editor order). Each of these is
//  duplicated with an "A_" / "B_" prefix in the layout below.
// -----------------------------------------------------------------------------

float SattariGranulatorAudioProcessor::layerParam (Layer layer, const char* baseId) const noexcept
{
    // Build the prefixed id on the stack (no heap alloc): "A_"/"B_" + baseId.
    char buf[64];
    const char* pre = layerPrefix (layer);   // 2-char prefix ("A_" / "B_")
    buf[0] = pre[0];
    buf[1] = pre[1];
    int n = 2;
    for (int k = 0; baseId[k] != '\0' && n < 63; ++k, ++n)
        buf[n] = baseId[k];
    buf[n] = '\0';
    return apvts.getRawParameterValue (buf)->load();
}

juce::AudioProcessorValueTreeState::ParameterLayout
SattariGranulatorAudioProcessor::createParameterLayout()
{
    // Parameters come from the module: one source of truth for both shells.
    sattari::dsp::SattariModule::ParamList p;
    sattari::dsp::GranulatorModule().addParameters (p, "");
    return { p.begin(), p.end() };
}


void SattariGranulatorAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    sampleRate = sr > 0.0 ? sr : 44100.0;
    module.link (apvts, "");
    module.prepare (sampleRate, juce::jmax (1, samplesPerBlock), getTotalNumOutputChannels());
    setLatencySamples (0);
}

void SattariGranulatorAudioProcessor::releaseResources() {}

void SattariGranulatorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Live Capture records the plugin's audio INPUT. The module has no buses, so
    // the discrete input bus is handed down through setSidechain() — the same hook
    // Comp and Gate use for an external key.
    const juce::AudioBuffer<float>* captureIn = nullptr;
    juce::AudioBuffer<float> inView;
    if (getBusCount (true) > 0)
        if (auto* inBus = getBus (true, 0); inBus != nullptr && inBus->isEnabled())
        {
                // setDataToReferTo, NOT assignment: AudioBuffer::operator= calls
                // setSize(..., avoidReallocating=false), so it REALLOCATES AND COPIES
                // on every block — an allocation on the audio thread. This re-points
                // at the host's channels and owns nothing.
            {
                auto in = getBusBuffer (buffer, true, 0);
                inView.setDataToReferTo (in.getArrayOfWritePointers(),
                                         in.getNumChannels(), in.getNumSamples());
            }
            if (inView.getNumChannels() > 0 && inView.getNumSamples() > 0)
                captureIn = &inView;
        }

    auto outBus = getBusBuffer (buffer, false, 0);

    module.setTransport (sattari::host::read (*this));
    module.setSidechain (captureIn);
    module.process (outBus, midi);
}

const std::vector<juce::String>& SattariGranulatorAudioProcessor::perLayerParamIds()
{
    return sattari::dsp::GranulatorModule::perLayerParamIds();
}

bool SattariGranulatorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Optional input bus: allow it DISABLED (pure synth) OR mono/stereo (capture).
    const auto& in = layouts.getMainInputChannelSet();
    if (in.isDisabled())
        return true;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

// =============================================================================
//  Source build / swap (message thread) — now per layer.
// =============================================================================
std::shared_ptr<const DspSource>
SattariGranulatorAudioProcessor::toDspSource (const std::shared_ptr<const GranSource>& s)
{
    if (! s) return nullptr;
    auto d = std::make_shared<DspSource>();
    d->audio            = s->audio;              // shares the underlying buffer copy
    d->sourceSampleRate = s->sourceSampleRate;
    d->lengthSamples    = s->lengthSamples;
    d->rootNote         = s->rootNote;
    d->version          = s->version;
    return d;
}

void SattariGranulatorAudioProcessor::publishSource (Layer layer,
                                                     std::shared_ptr<const GranSource> next,
                                                     bool pushUndo)
{
    auto& ls = L (layer);
    if (pushUndo)
        if (auto prev = ls.sourceMsg)
        {
            ls.undoStack.push_back (prev);
            if ((int) ls.undoStack.size() > kMaxUndo) ls.undoStack.erase (ls.undoStack.begin());
        }
    ls.sourceMsg = next;
    // Only the dsp view crosses the thread boundary; the module atomic-swaps it.
    module.setSource (layer, toDspSource (next));
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::applyChannelMode (const std::shared_ptr<const GranSource>& src, int mode)
{
    if (! src || src->lengthSamples <= 0 || mode == 0)   // 0 = Stereo (as-is)
        return src;

    const int len = src->lengthSamples;
    const int inCh = src->audio.getNumChannels();
    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src->sourceSampleRate;
    out->lengthSamples    = len;
    out->rootNote         = src->rootNote;
    out->version          = src->version + 1;

    if (mode == 1)   // Mono: average all channels
    {
        out->audio.setSize (1, len);
        out->audio.clear();
        auto* d = out->audio.getWritePointer (0);
        for (int ch = 0; ch < inCh; ++ch)
        {
            const float* s = src->audio.getReadPointer (ch);
            for (int i = 0; i < len; ++i) d[i] += s[i] / (float) inCh;
        }
    }
    else   // 2 = Left, 3 = Right (pick one channel, keep it mono)
    {
        const int pick = juce::jlimit (0, inCh - 1, mode == 2 ? 0 : 1);
        out->audio.setSize (1, len);
        out->audio.copyFrom (0, 0, src->audio, pick, 0, len);
    }
    return out;
}

bool SattariGranulatorAudioProcessor::loadFile (Layer layer, const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int len = (int) juce::jmin ((juce::int64) (48 * 60 * 192000),   // hard sanity cap
                                      reader->lengthInSamples);
    const int ch  = (int) juce::jmax (1u, juce::jmin (2u, reader->numChannels));

    auto built = std::make_shared<GranSource>();
    built->audio.setSize (ch, len);
    built->audio.clear();
    reader->read (&built->audio, 0, len, 0, true, ch > 1);
    built->sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : sampleRate;
    built->lengthSamples    = len;
    built->rootNote         = 60;   // C4 default; retune via Coarse/Fine
    built->version          = 1;

    // Apply the layer's current channel-mode downmix at build time.
    const int monoMode = (int) std::lround (layerParam (layer, "srcMono"));
    auto shaped = applyChannelMode (built, monoMode);

    publishSource (layer, shaped, /*pushUndo=*/true);

    LayerMsg& ls = L (layer);
    ls.sourcePath = file.getFullPathName();
    ls.sourceHash = juce::String (file.hashCode64());
    return true;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::currentSource (Layer layer) const
{
    return std::atomic_load (&L (layer).sourceMsg);
}

juce::String SattariGranulatorAudioProcessor::currentSourceName (Layer layer) const
{
    const auto& ls = L (layer);
    if (ls.sourcePath.isNotEmpty())
        return juce::File (ls.sourcePath).getFileNameWithoutExtension();
    // A captured (non-file) buffer has no path but a live source: label it.
    if (std::atomic_load (&ls.sourceMsg) != nullptr)
        return "(captured)";
    return {};
}

// =============================================================================
//  Destructive edit primitives (pure SourceSample -> SourceSample)
// =============================================================================
std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeReversed (const GranSource& src)
{
    const int len = src.lengthSamples, ch = src.audio.getNumChannels();
    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (ch, len);
    for (int c = 0; c < ch; ++c)
    {
        const float* s = src.audio.getReadPointer (c);
        float* d = out->audio.getWritePointer (c);
        for (int i = 0; i < len; ++i) d[i] = s[len - 1 - i];
    }
    return out;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeNormalized (const GranSource& src)
{
    const int len = src.lengthSamples, ch = src.audio.getNumChannels();
    float peak = 0.0f;
    for (int c = 0; c < ch; ++c)
        peak = juce::jmax (peak, src.audio.getMagnitude (c, 0, len));
    const float target = juce::Decibels::decibelsToGain (-0.5f);
    const float gain = peak > 1.0e-6f ? target / peak : 1.0f;

    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (ch, len);
    for (int c = 0; c < ch; ++c)
    {
        out->audio.copyFrom (c, 0, src.audio, c, 0, len);
        out->audio.applyGain (c, 0, len, gain);
    }
    return out;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeTrimmed (const GranSource& src, float start01, float end01)
{
    const int srcLen = src.lengthSamples, ch = src.audio.getNumChannels();
    const int a = juce::jlimit (0, juce::jmax (0, srcLen - 1),
                                (int) std::floor (juce::jlimit (0.0f, 1.0f, start01) * (float) srcLen));
    int b = juce::jlimit (0, srcLen,
                          (int) std::ceil (juce::jlimit (0.0f, 1.0f, end01) * (float) srcLen));
    if (b <= a + 1) b = juce::jmin (srcLen, a + 1);
    const int len = b - a;

    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (ch, len);
    for (int c = 0; c < ch; ++c)
        out->audio.copyFrom (c, 0, src.audio, c, a, len);
    return out;
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::makeMonoFold (const GranSource& src)
{
    const int len = src.lengthSamples, ch = src.audio.getNumChannels();
    auto out = std::make_shared<GranSource>();
    out->sourceSampleRate = src.sourceSampleRate;
    out->lengthSamples = len; out->rootNote = src.rootNote; out->version = src.version + 1;
    out->audio.setSize (1, len);
    out->audio.clear();
    auto* d = out->audio.getWritePointer (0);
    for (int c = 0; c < ch; ++c)
    {
        const float* s = src.audio.getReadPointer (c);
        for (int i = 0; i < len; ++i) d[i] += s[i] / (float) juce::jmax (1, ch);
    }
    return out;
}

// =============================================================================
//  Destructive edit ops (message thread; each rebuild + swap) — per layer.
// =============================================================================
void SattariGranulatorAudioProcessor::editReverse (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg)) publishSource (layer, makeReversed (*s), true);
}

void SattariGranulatorAudioProcessor::editNormalize (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg)) publishSource (layer, makeNormalized (*s), true);
}

void SattariGranulatorAudioProcessor::editTrimToSelection (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg))
    {
        const auto startId = layerParamId (layer, "srcStart");
        const auto endId   = layerParamId (layer, "srcEnd");
        publishSource (layer, makeTrimmed (*s, layerParam (layer, "srcStart"),
                                              layerParam (layer, "srcEnd")), true);
        // Trim resets the region back to the whole (new) buffer.
        if (auto* ps = apvts.getParameter (startId)) ps->setValueNotifyingHost (0.0f);
        if (auto* pe = apvts.getParameter (endId))   pe->setValueNotifyingHost (1.0f);
    }
}

void SattariGranulatorAudioProcessor::editMonoFold (Layer layer)
{
    if (auto s = std::atomic_load (&L (layer).sourceMsg)) publishSource (layer, makeMonoFold (*s), true);
}

void SattariGranulatorAudioProcessor::editUndo (Layer layer)
{
    LayerMsg& ls = L (layer);
    if (ls.undoStack.empty()) return;
    auto prev = ls.undoStack.back();
    ls.undoStack.pop_back();
    publishSource (layer, prev, /*pushUndo=*/false);
}

bool SattariGranulatorAudioProcessor::canUndo (Layer layer) const noexcept
{
    return ! L (layer).undoStack.empty();
}

// =============================================================================
//  Live Capture (message thread build; audio thread only ring-writes)
// =============================================================================
void SattariGranulatorAudioProcessor::armCapture (bool arm) noexcept
{
    if (arm)
    {
        const float secs = juce::jlimit (0.5f, 8.0f, param ("captureSecs"));
        module.armCaptureRing ((int) std::lround (param ("captureTarget")),
                               (int) std::round (secs * sampleRate),
                               sampleRate);
    }
    else
    {
        module.disarmCaptureRing();
    }
}

float SattariGranulatorAudioProcessor::captureProgress() const noexcept
{
    if (! module.captureArmed()) return 0.0f;
    const float secs = juce::jlimit (0.5f, 8.0f, param ("captureSecs"));
    const int target = juce::jmax (1, (int) std::round (secs * module.captureSampleRate()));
    return juce::jlimit (0.0f, 1.0f, (float) module.captureFilled() / (float) target);
}

std::shared_ptr<const GranSource>
SattariGranulatorAudioProcessor::buildCaptureSource (const std::vector<float>& mono,
                                                     int lengthSamples, double captureSr) const
{
    auto out = std::make_shared<GranSource>();
    const int len = juce::jmax (0, lengthSamples);
    out->audio.setSize (1, juce::jmax (1, len));
    out->audio.clear();
    if (len > 0)
    {
        float* d = out->audio.getWritePointer (0);
        for (int i = 0; i < len; ++i) d[i] = mono[(size_t) i];
    }
    out->sourceSampleRate = captureSr > 0.0 ? captureSr : sampleRate;
    out->lengthSamples    = len;
    out->rootNote         = 60;
    out->version          = 1;
    return out;
}

bool SattariGranulatorAudioProcessor::serviceCapture()
{
    // React to the momentary captureArm param edge (the editor pulses it). This
    // edge detection is what actually ARMS the ring — dropping it in the move
    // silently produced a capture that never started.
    const bool armParam = param ("captureArm") > 0.5f;
    if (armParam != lastArmParam)
    {
        lastArmParam = armParam;
        armCapture (armParam);
        if (armParam) return false;   // just armed; nothing to drain yet
    }

    // MESSAGE THREAD. The module's ring is written by the audio thread; here we
    // copy it out, build a SourceSample and publish it back. This is the one
    // place in the suite where data flows audio -> message -> audio, which is
    // why the ring lives with the writer and is drained through accessors.
    const bool done       = module.captureDone();
    const bool stillArmed = module.captureArmed();
    const int  filled     = module.captureFilled();

    if (done || (! stillArmed && filled > 0))
    {
        const int len = juce::jmin (filled, module.captureCapacity());
        if (len > 0)
        {
            std::vector<float> mono ((size_t) len, 0.0f);
            const auto& ring = module.captureData();
            for (int i = 0; i < len; ++i) mono[(size_t) i] = ring[(size_t) i];

            const int targetLayerIdx = module.captureTargetLayer();
            const Layer target = (targetLayerIdx == 1) ? Layer::B : Layer::A;

            auto src = buildCaptureSource (mono, len, module.captureSampleRate());
            publishSource (target, src, /*pushUndo=*/true);
            L (target).sourcePath.clear();          // captured, not loaded from disk
            L (target).sourceHash.clear();
        }
        module.disarmCaptureRing();
        return len > 0;
    }
    return false;
}

bool SattariGranulatorAudioProcessor::serviceKeyBroadcast()
{
    if (! module.broadcastWanted())
        return false;

    const int root  = juce::jlimit (0, 11, module.broadcastRoot());
    const int scale = juce::jlimit (0, 1,  module.broadcastScale());

    const juce::int64 now = juce::Time::currentTimeMillis();
    const bool changed = (root != lastBroadcastRoot) || (scale != lastBroadcastScale);
    // Republish on change immediately; otherwise refresh at most ~1 Hz so a held
    // note keeps the bus entry alive without thrashing the file.
    if (! changed && (now - lastBroadcastMs) < 1000)
        return false;

    lastBroadcastRoot  = root;
    lastBroadcastScale = scale;
    lastBroadcastMs    = now;

    // Publish the full session (key + tempo + transient id). We don't own a tempo
    // source, so publish bpm 0 (readers keep their own tempo); the ++id keeps the
    // session message fresh for followers.
    sattari::xplugin::publishSession ("granulator", root, scale, 0.0, ++broadcastId);
    return true;
}

// =============================================================================
//  State (per-layer path + hash + version tag; graceful "sample missing")
// =============================================================================
void SattariGranulatorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("granulatorStateVersion", kStateVersion, nullptr);
    // Per-layer source file paths + content hashes.
    state.setProperty ("srcFilePathA", L (Layer::A).sourcePath, nullptr);
    state.setProperty ("srcFileHashA", L (Layer::A).sourceHash, nullptr);
    state.setProperty ("srcFilePathB", L (Layer::B).sourcePath, nullptr);
    state.setProperty ("srcFileHashB", L (Layer::B).sourceHash, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SattariGranulatorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    const int version = (int) tree.getProperty ("granulatorStateVersion", 1);

    // Legacy (v1) single-layer presets store `srcFilePath` / `srcFileHash` and
    // un-prefixed per-layer params. Migrate every legacy per-layer PARAM value
    // into its A_<base> node so a recalled patch sounds like it did (Layer B is
    // left at defaults). APVTS stores params as "PARAM" nodes with id/value.
    if (version < 2)
    {
        for (const auto& base : perLayerParamIds())
        {
            if (base == "level" || base == "pan")   // new params: no legacy source
                continue;
            auto legacyNode = tree.getChildWithProperty ("id", base);
            auto aNode      = tree.getChildWithProperty ("id", "A_" + base);
            if (legacyNode.isValid() && aNode.isValid())
                aNode.setProperty ("value", legacyNode.getProperty ("value"), nullptr);
        }
    }

    juce::String pathA = tree.getProperty ("srcFilePathA", juce::String()).toString();
    juce::String hashA = tree.getProperty ("srcFileHashA", juce::String()).toString();
    juce::String pathB = tree.getProperty ("srcFilePathB", juce::String()).toString();
    juce::String hashB = tree.getProperty ("srcFileHashB", juce::String()).toString();
    if (version < 2)   // legacy single path lands in A
    {
        pathA = tree.getProperty ("srcFilePath", pathA).toString();
        hashA = tree.getProperty ("srcFileHash", hashA).toString();
    }

    apvts.replaceState (tree);

    auto recall = [this] (Layer layer, const juce::String& path, const juce::String& hash)
    {
        LayerMsg& ls = L (layer);
        if (path.isEmpty()) return;
        const juce::File f (path);
        if (f.existsAsFile() && loadFile (layer, f))
            ls.sourceHash = hash.isNotEmpty() ? hash : ls.sourceHash;
        else
        {
            ls.sourcePath = path;   // remember the intended file for the UI label
            ls.sourceHash = hash;
        }
    };
    recall (Layer::A, pathA, hashA);
    recall (Layer::B, pathB, hashB);
}

// =============================================================================
//  Editor + factory
// =============================================================================
juce::AudioProcessorEditor* SattariGranulatorAudioProcessor::createEditor()
{
    return new SattariGranulatorAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariGranulatorAudioProcessor();
}
