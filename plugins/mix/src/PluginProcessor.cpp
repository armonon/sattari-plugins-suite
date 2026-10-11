#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MixController.h"

#include <sattari_audio_core/modules/AlignModule.h>
#include <sattari_audio_core/modules/CleanModule.h>
#include <sattari_audio_core/modules/CompModule.h>
#include <sattari_audio_core/modules/DeEssModule.h>
#include <sattari_audio_core/modules/EqModule.h>
#include <sattari_audio_core/modules/GateModule.h>
#include <sattari_audio_core/modules/HeatModule.h>
#include <sattari_audio_core/modules/ImageModule.h>
#include <sattari_audio_core/modules/MeterModule.h>
#include <sattari_audio_core/modules/MultibandModule.h>
#include <sattari_audio_core/modules/PeakModule.h>
#include <sattari_audio_core/modules/PulseModule.h>
#include <sattari_audio_core/modules/SideChainMasterModule.h>
#include <sattari_audio_core/modules/TransientModule.h>

// The rack's module roster. Adding a module here is the ONLY step needed to make
// it available in every slot — its parameters, preparation and reset all follow.
//
// !! THIS ORDER IS FROZEN. APPEND ONLY. !!
// A slot stores an INDEX into this list (0 = Empty, N = modules[N-1]), so
// inserting or reordering a module silently rewires every saved session and
// preset to a different processor. New modules go at the END, alphabetical or
// not. The menu is cosmetic; the order is data.
static std::vector<std::unique_ptr<sattari::dsp::SattariModule>> makeMixModules()
{
    std::vector<std::unique_ptr<sattari::dsp::SattariModule>> v;
    v.push_back (std::make_unique<sattari::dsp::CleanModule>());              // 1
    v.push_back (std::make_unique<sattari::dsp::CompModule>());               // 2
    v.push_back (std::make_unique<sattari::dsp::DeEssModule>());              // 3
    v.push_back (std::make_unique<sattari::dsp::EqModule>());                 // 4
    v.push_back (std::make_unique<sattari::dsp::GateModule>());               // 5
    v.push_back (std::make_unique<sattari::dsp::HeatModule>());               // 6
    v.push_back (std::make_unique<sattari::dsp::ImageModule>());              // 7
    v.push_back (std::make_unique<sattari::dsp::MultibandModule>());          // 8
    v.push_back (std::make_unique<sattari::dsp::MeterModule>());              // 9
    v.push_back (std::make_unique<sattari::dsp::PeakModule>());               // 10
    v.push_back (std::make_unique<sattari::dsp::PulseModule>());              // 11
    v.push_back (std::make_unique<sattari::dsp::TransientModule>());          // 12
    v.push_back (std::make_unique<sattari::dsp::SideChainMasterModule>());    // 13
    v.push_back (std::make_unique<sattari::dsp::AlignModule>());              // 14
    return v;
}

SattariMixAudioProcessor::SattariMixAudioProcessor()
    : sattari::rack::SattariRackProcessor (
          "Sattari Mix", "MIX",
          BusesProperties()
              .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
              // Optional external key. A channel strip is expected to compress a bass
              // to a kick or gate a snare to a trigger; without this bus the rack
              // structurally cannot, and two-signal modules (Align, Side Chain Master)
              // cannot exist at all.
              .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
              .withOutput ("Output",    juce::AudioChannelSet::stereo(), true),
          &makeMixModules, "mix", &addAssistParameters)
{
    registerFactoryPresets();
    const char* ids[] = { "auto.gain", "auto.low", "auto.presence", "auto.compression",
                         "auto.threshold", "auto.amount", "auto.enabled", "auto.role", "auto.lock", "auto.isBus" };
    for (size_t i = 0; i < controls.size(); ++i) controls[i] = apvts.getRawParameterValue (ids[i]);
    rackParameterCount = getParameters().size() - (int) controls.size();
    label = "Track " + session.id.substring (0, 4);
    controller = std::make_unique<MixController> (*this);
    startTimerHz (4);
}

SattariMixAudioProcessor::~SattariMixAudioProcessor() { stopTimer(); }

void SattariMixAudioProcessor::addAssistParameters (sattari::dsp::SattariModule::ParamList& p)
{
    using F = juce::AudioParameterFloat;
    p.push_back (std::make_unique<F> (juce::ParameterID { "auto.gain", 1 }, "Auto Mix Gain", -36.0f, 12.0f, 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "auto.low", 1 }, "Auto Mix Low EQ", -6.0f, 0.0f, 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "auto.presence", 1 }, "Auto Mix Presence EQ", -6.0f, 0.0f, 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "auto.compression", 1 }, "Auto Mix Dynamics", 0.0f, 1.0f, 0.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "auto.threshold", 1 }, "Auto Mix Threshold", -48.0f, -6.0f, -18.0f));
    p.push_back (std::make_unique<F> (juce::ParameterID { "auto.amount", 1 }, "Auto Mix Amount", 0.0f, 1.0f, 1.0f));
    p.push_back (std::make_unique<sattari::parameters::Bool> (juce::ParameterID { "auto.enabled", 1 }, "Auto Mix Enabled", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "auto.role", 1 }, "Auto Mix Role", sattari::automix::roleNames(), 0));
    p.push_back (std::make_unique<sattari::parameters::Bool> (juce::ParameterID { "auto.lock", 1 }, "Auto Mix Lock", false));
    p.push_back (std::make_unique<sattari::parameters::Bool> (juce::ParameterID { "auto.isBus", 1 }, "Summing Bus Insert", false));
}

SattariMixAudioProcessor::Settings SattariMixAudioProcessor::assistSettings() const noexcept
{
    Settings s;
    s.gain = controls[0]->load(); s.low = controls[1]->load(); s.presence = controls[2]->load();
    s.compression = controls[3]->load(); s.threshold = controls[4]->load(); s.amount = controls[5]->load();
    s.enabled = controls[6]->load() > 0.5f; s.trim = monitorTrim.load();
    return s;
}

void SattariMixAudioProcessor::setAssistSettings (const Settings& settings)
{
    using namespace sattari::automix;
    const auto s = decodeSettings (encode (settings));
    const char* ids[] = { "auto.gain", "auto.low", "auto.presence", "auto.compression", "auto.threshold", "auto.amount", "auto.enabled" };
    const float values[] = { s.gain, s.low, s.presence, s.compression, s.threshold, s.amount, s.enabled ? 1.0f : 0.0f };
    for (size_t i = 0; i < std::size (values); ++i)
    {
        auto* p = apvts.getParameter (ids[i]);
        if (std::abs (p->convertFrom0to1 (p->getValue()) - values[i]) < 0.0001f) continue;
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (values[i])); p->endChangeGesture();
    }
    monitorTrim.store (s.trim);
}

void SattariMixAudioProcessor::prepareToPlay (double sr, int block)
{
    const auto generation = requestedCapture.load();
    const bool pending = generation != audioCapture;
    const bool completed = analyzer.read().complete;
    SattariRackProcessor::prepareToPlay (sr, block);
    analyzer.prepare (sr); assist.prepare (sr, block); assist.prime (assistSettings());
    audioCapture = generation;
    if (pending || (captureSeconds.load() > 0 && ! completed)) --audioCapture;
    captureChanged.store (completed);
    publishedCapture.store (audioCapture);
}

void SattariMixAudioProcessor::reset()
{
    SattariRackProcessor::reset(); assist.reset(); assist.prime (assistSettings());
}

void SattariMixAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    SattariRackProcessor::processBlock (buffer, midi);
    auto main = getBusBuffer (buffer, true, 0);
    const auto wanted = requestedCapture.load();
    if (wanted != audioCapture)
    {
        analyzer.reset (captureSeconds.load());
        captureChanged.store (false);
        audioCapture = wanted; publishedCapture.store (audioCapture);
    }
    if (captureSeconds.load() > 0 && capturedRack.load() != rackFingerprint()) captureChanged.store (true);
    analyzer.process (main);
    assist.process (main, assistSettings());
}

juce::String SattariMixAudioProcessor::groupName() const { const juce::ScopedLock lock (identityMutex); return group; }
juce::String SattariMixAudioProcessor::trackName() const { const juce::ScopedLock lock (identityMutex); return label; }

void SattariMixAudioProcessor::setIdentity (const juce::String& newGroup, const juce::String& name)
{
    const juce::ScopedLock lock (identityMutex);
    const auto cleaned = newGroup.trim().substring (0, 80);
    if (group != cleaned) { group = cleaned; joinPending.store (true); }
    label = name.trim().substring (0, 80);
    if (label.isEmpty()) label = "Track " + session.id.substring (0, 4);
    customLabel = true;
}

void SattariMixAudioProcessor::updateTrackProperties (const TrackProperties& props)
{
    const juce::ScopedLock lock (identityMutex);
    if (! customLabel && props.name.has_value() && props.name->isNotEmpty()) label = props.name->substring (0, 80);
}

std::uint64_t SattariMixAudioProcessor::rackFingerprint() const
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < rackParameterCount; ++i)
    {
        const float v = getParameters()[i]->getValue();
        std::uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        hash = (hash ^ bits) * 1099511628211ULL;
    }
    return hash;
}

void SattariMixAudioProcessor::requestAnalysis (const juce::String& capture, int seconds)
{
    captureId = capture; capturedRack.store (rackFingerprint()); captureChanged.store (false);
    captureSeconds.store (juce::jlimit (0, 30, seconds));
    requestedCapture.fetch_add (1);
}

SattariMixAudioProcessor::Track SattariMixAudioProcessor::trackSnapshot() const
{
    Track t;
    t.id = session.id; t.name = trackName(); t.role = (sattari::automix::Role) (int) controls[7]->load();
    t.locked = controls[8]->load() > 0.5f; t.settings = assistSettings();
    t.isBus = controls[9]->load() >= .5f;
    t.ack = lastCommand; t.error = commandError;
    if (captureId.isNotEmpty() && publishedCapture.load() == requestedCapture.load())
    {
        t.measured = analyzer.read(); t.capture = captureId;
        if (captureChanged.load() || capturedRack.load() != rackFingerprint())
        { t.measured.invalid = true; t.measured.complete = true; t.measured.progress = 1; }
    }
    return t;
}

void SattariMixAudioProcessor::syncSession()
{
    using namespace sattari::automix;
    if (joinPending.exchange (false))
    {
        transportError = session.join (groupName()) ? juce::String() : "Cannot open the mix group directory.";
        captureId.clear(); lastCommand.clear(); commandError.clear(); trimOwner.clear(); monitorTrim.store (0);
        captureSeconds.store (0); requestedCapture.fetch_add (1);
        controller->resetForGroup();
    }
    if (! session.linked()) { controller->poll(); return; }
    auto command = session.receive();
    const auto token = get (command, "token").toString();
    if (token.isNotEmpty() && token != lastCommand)
    {
        lastCommand = token; commandError.clear();
        const auto action = get (command, "action").toString();
        if (action == "analyze")
            requestAnalysis (token, (int) number (command, "seconds", 12, 1, 30));
        else if (action == "stop") requestAnalysis ({}, 0);
        else if (action == "configure")
        {
            if ((int) controls[7]->load() != (int) get (command, "beforeRole")
                || (controls[8]->load() > 0.5f) != (bool) get (command, "beforeLock"))
                commandError = "Role or lock changed; try again.";
            else
            {
                auto* role = apvts.getParameter ("auto.role");
                auto* locked = apvts.getParameter ("auto.lock");
                role->beginChangeGesture();
                role->setValueNotifyingHost (role->convertTo0to1 (number (command, "role", 0, 0, 6)));
                role->endChangeGesture();
                locked->beginChangeGesture(); locked->setValueNotifyingHost ((bool) get (command, "locked") ? 1.0f : 0.0f); locked->endChangeGesture();
            }
        }
        else if (action == "settings" && get (command, "before").isObject() && get (command, "after").isObject())
        {
            const auto before = decodeSettings (get (command, "before"));
            const auto after = decodeSettings (get (command, "after"));
            auto noTrim = after; noTrim.trim = before.trim;
            const bool locked = controls[8]->load() > 0.5f;
            if (! equivalent (before, assistSettings())) commandError = "Settings changed; refresh the proposal.";
            else if (locked && ! equivalent (before, noTrim)) commandError = "Track is locked.";
            else if (get (command, "capture").toString().isNotEmpty()
                     && (get (command, "capture").toString() != trackSnapshot().capture
                         || trackSnapshot().measured.invalid))
                commandError = "Analysis changed; analyze the group again.";
            else
            {
                setAssistSettings (after);
                recordDecision ("Applied: gain " + juce::String (after.gain, 1) + " dB; low " + juce::String (after.low, 1)
                    + " dB; presence " + juce::String (after.presence, 1) + " dB; dynamics " + juce::String (after.compression * 100, 0) + "%");
                trimOwner = after.trim < -0.001f ? get (command, "sender").toString() : juce::String();
            }
        }
        else commandError = "Unsupported mix command.";
    }
    if (trimOwner.isNotEmpty() && trimOwner != session.id)
    {
        if (! session.isAlive (trimOwner)) { monitorTrim.store (0); trimOwner.clear(); }
    }
    if (! session.publish (trackSnapshot())) transportError = "Cannot publish to the mix group.";
    else transportError.clear();
    controller->poll();
}

void SattariMixAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock lock (identityMutex);
        state.setProperty ("autoMixGroup", group, nullptr);
        state.setProperty ("autoMixName", label, nullptr);
        state.setProperty ("autoMixCustomName", customLabel, nullptr);
    }
    { const juce::ScopedLock lock (historyMutex); state.setProperty ("autoMixHistory", history.joinIntoString ("\n"), nullptr); }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SattariMixAudioProcessor::recordDecision (const juce::String& text)
{
    const juce::ScopedLock lock (historyMutex);
    history.insert (0, juce::Time::getCurrentTime().toISO8601 (true) + "  " + text.replaceCharacters ("\r\n", "  ").substring (0, 2048));
    while (history.size() > 16) history.remove (history.size() - 1);
}

juce::String SattariMixAudioProcessor::decisionHistory() const
{ const juce::ScopedLock lock (historyMutex); return history.joinIntoString ("\n\n"); }

void SattariMixAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;
    // Older sessions have no assistant parameters. Loading one must clear any
    // assistant settings left by a previous session in this processor instance.
    const char* ids[] = { "auto.gain", "auto.low", "auto.presence", "auto.compression", "auto.threshold",
                         "auto.amount", "auto.enabled", "auto.role", "auto.lock", "auto.isBus" };
    for (const auto* id : ids)
        if (! state.getChildWithProperty ("id", id).isValid())
        {
            juce::ValueTree param ("PARAM");
            const auto* p = apvts.getParameter (id);
            param.setProperty ("id", id, nullptr);
            param.setProperty ("value", p->convertFrom0to1 (p->getDefaultValue()), nullptr);
            state.appendChild (param, nullptr);
        }
    {
        const juce::ScopedLock lock (identityMutex);
        group = state.getProperty ("autoMixGroup").toString().substring (0, 80);
        label = state.getProperty ("autoMixName", "Track " + session.id.substring (0, 4)).toString().substring (0, 80);
        customLabel = (bool) state.getProperty ("autoMixCustomName", false);
    }
    {
        const juce::ScopedLock lock (historyMutex);
        history = juce::StringArray::fromLines (state.getProperty ("autoMixHistory").toString().substring (0, 34000));
        history.removeEmptyStrings(); while (history.size() > 16) history.remove (history.size() - 1);
    }
    apvts.replaceState (state); monitorTrim.store (0); joinPending.store (true);
}

// Factory presets. Two kinds, and the second is the point of the whole rack:
//
//  * ordinary starting chains (Vocal Chain, Drum Bus, ...);
//  * COLLAPSED PLUGINS — "Bus" and "Royal Chain" were separate plugins whose only
//    real content was a FIXED order of these same processors. A fixed chain is a
//    preset, not a product, so they live here now as wiring. The difference for
//    the user is that the order is no longer welded shut.
//
// Only the parameters a preset actually cares about are set; everything else keeps
// its default, so presets stay valid as modules gain parameters.
void SattariMixAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    addChainPreset ("Vocal Chain",
        { "gate", "deess", "eq", "comp", "heat" },
        PV{ {"gate.threshold", -45}, {"gate.range", 40},
            {"deess.threshold", -30}, {"deess.range", 8},
            {"eq.hpf", 90},
            {"comp.threshold", -18}, {"comp.ratio", 3.0f}, {"comp.attack", 8}, {"comp.release", 120},
            {"heat.drive", 0.25f} });

    addChainPreset ("Drum Bus",
        { "transient", "comp", "multiband", "heat", "peak" },
        PV{ {"transient.attack", 0.35f}, {"transient.sustain", -0.15f},
            {"comp.threshold", -14}, {"comp.ratio", 4.0f}, {"comp.attack", 15}, {"comp.release", 90},
            {"heat.drive", 0.3f} });

    addChainPreset ("Master Bus",
        { "eq", "multiband", "image", "peak", "meter" },
        PV{ {"eq.hpf", 25},
            {"peak.ceiling", -1.0f} });

    // Stabilise bass-heavy buses without turning the whole mix into a loudness
    // preset: low-band control first, then transparent peak safety and metering.
    addChainPreset ("Low-End Control",
        { "eq", "multiband", "comp", "peak", "meter" },
        PV{ {"eq.hpf", 24.0f},
            {"multiband.xLow", 180.0f}, {"multiband.thr1", -22.0f},
            {"multiband.ratio1", 3.2f}, {"multiband.atk1", 22.0f},
            {"multiband.rel1", 180.0f},
            {"comp.threshold", -15.0f}, {"comp.ratio", 1.8f},
            {"comp.attack", 28.0f}, {"comp.release", 170.0f},
            {"peak.ceiling", -1.0f}, {"peak.truePeak", 1.0f} });

    addChainPreset ("Clean Up",
        { "clean", "gate", "eq", "deess" },
        PV{ {"gate.threshold", -50}, {"eq.hpf", 80} });

    addChainPreset ("Duck To Kick",
        { "sidechain", "eq", "comp" },
        PV{ {"sidechain.external", 1}, {"sidechain.depth", 8}, {"sidechain.release", 160} });

    // --- Collapsed plugins ---------------------------------------------------
    addChainPreset ("Bus (classic)",
        { "eq", "comp", "heat", "image", "peak" },
        PV{ {"eq.hpf", 30},
            {"comp.threshold", -16}, {"comp.ratio", 2.5f}, {"comp.attack", 20}, {"comp.release", 150},
            {"heat.drive", 0.2f},
            {"image.width", 1.1f},
            {"peak.ceiling", -0.3f} });

    addChainPreset ("Royal Chain (classic)",
        { "gate", "deess", "eq", "comp", "multiband", "heat", "peak" },
        PV{ {"gate.threshold", -48}, {"gate.range", 35},
            {"deess.threshold", -32}, {"deess.range", 10},
            {"eq.hpf", 95},
            {"comp.threshold", -20}, {"comp.ratio", 3.5f}, {"comp.attack", 6}, {"comp.release", 110},
            {"heat.drive", 0.3f},
            {"peak.ceiling", -0.5f} });
}

juce::AudioProcessorEditor* SattariMixAudioProcessor::createEditor()
{
    return new SattariMixAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SattariMixAudioProcessor(); }
