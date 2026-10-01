#include "../src/PluginProcessor.h"
#include "../src/MixController.h"
#include <rack_product_contract.h>

#include <cstdio>
#include <limits>

using namespace sattari::automix;
using sattari::test::setRealParameter;
static int failures = 0;
static void check (bool condition, const char* text)
{ if (! condition) { std::printf ("FAIL: %s\n", text); ++failures; } }

static bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < a.getNumChannels(); ++c)
        if (std::memcmp (a.getReadPointer (c), b.getReadPointer (c), (size_t) a.getNumSamples() * sizeof (float)) != 0) return false;
    return true;
}

static void fill (juce::AudioBuffer<float>& b, std::int64_t offset, double sr, float level, double hz, bool opposite = false)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (c, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) (offset + i) / sr)
                * (opposite && c == 1 ? -1.0f : 1.0f));
}

static Measurement measure (int block, bool opposite, bool silence)
{
    Analyzer a; a.prepare (48000); a.reset (4);
    juce::AudioBuffer<float> b (2, block);
    for (int offset = 0; offset < 48000 * 4; offset += block)
    {
        fill (b, offset, 48000, silence && offset >= 96000 ? 0.0f : 0.1f, 1000, opposite);
        a.process (b);
    }
    return a.read();
}

static void analyzerContract()
{
    const auto a = measure (64, false, false), b = measure (511, true, false), sparse = measure (64, false, true);
    check (a.complete && b.complete && sparse.complete, "capture completes for variable block sizes");
    check (std::abs (a.loudness - b.loudness) < 0.01f, "stereo cancellation and block size do not bias analysis");
    check (std::abs (a.loudness - sparse.loudness) < 0.2f, "silent tails do not cause gain chasing");
    check (sparse.activeSeconds < 2.3f, "active duration excludes silence");
    Analyzer bad; bad.prepare (48000); bad.reset (1);
    juce::AudioBuffer<float> buffer (2, 48000); buffer.clear();
    buffer.setSample (0, 1, std::numeric_limits<float>::quiet_NaN()); bad.process (buffer);
    check (bad.read().invalid && bad.read().complete, "nonfinite source rejects proposal");
}

static Track fixture (juce::String id, Role role, float level, float peak)
{
    Track t; t.id = id; t.name = id; t.role = role; t.capture = "pass";
    t.measured.complete = true; t.measured.activeSeconds = 4; t.measured.seconds = 4;
    t.measured.loudness = level; t.measured.peakDb = peak; t.measured.presence = 0.5f; t.measured.low = 0.3f;
    return t;
}

static void plannerContract()
{
    std::vector<Track> tracks { fixture ("vocal", Role::lead, -26, -14), fixture ("keys", Role::instrument, -12, -4), fixture ("bass", Role::bass, -15, -5) };
    auto p = makePlan (tracks, "pass");
    check (p.valid(), "vocal-centered plan is valid");
    check (p.estimatedPeakDb <= -5.99f, "conservative coincident sample peak bound");
    check (p.tracks[1].proposed.presence < 0 && p.tracks[1].proposed.low < 0, "support creates vocal and bass space");
    check (p.tracks[0].proposed.gain <= 6, "boost is bounded");
    tracks[1].locked = true; tracks[1].measured.peakDb = -30;
    p = makePlan (tracks, "pass");
    check (p.valid() && equivalent (p.tracks[1].proposed, tracks[1].settings), "locked settings are preserved");
    tracks[1].measured.peakDb = 0;
    check (! makePlan (tracks, "pass").valid(), "impossible locked headroom is reported");
    tracks[1].locked = false; tracks[1].capture = "old";
    check (! makePlan (tracks, "pass").valid(), "stale captures cannot enter a proposal");
    tracks[1].capture = "pass"; tracks[1].measured.activeSeconds = 0;
    p = makePlan (tracks, "pass");
    check (equivalent (p.tracks[1].proposed, tracks[1].settings), "silent track is not boosted");
    tracks[1].measured.activeSeconds = 4; tracks[1].measured.peakDb = 35;
    check (! makePlan (tracks, "pass").valid(), "unrepresentable gain correction is rejected");
}

static void render (SattariMixAudioProcessor& a, SattariMixAudioProcessor& b, int seconds)
{
    juce::AudioBuffer<float> x (2, 256), y (2, 256); juce::MidiBuffer midi;
    for (int offset = 0; offset < 48000 * seconds; offset += 256)
    {
        fill (x, offset, 48000, 0.05f, 700); fill (y, offset, 48000, 0.25f, 3000);
        a.processBlock (x, midi); b.processBlock (y, midi);
    }
}
static void pump (SattariMixAudioProcessor& a, SattariMixAudioProcessor& b)
{ for (int i = 0; i < 4; ++i) { a.syncSession(); b.syncSession(); } }

static void workflowContract (const juce::File& screenshots)
{
    SattariMixAudioProcessor a, b;
    const auto group = "contract-" + juce::Uuid().toString();
    a.setIdentity (group, "Lead vocal"); b.setIdentity (group, "Synth backing");
    a.prepareToPlay (48000, 256); b.prepareToPlay (48000, 256);
    setRealParameter (a.parameters(), "auto.role", (float) Role::lead);
    pump (a, b);
    auto& controller = a.mixController();
    check (a.linkedTracks().size() == 2, "two processors discover one group");
    controller.analyze (2); pump (a, b);
    a.prepareToPlay (48000, 256); b.prepareToPlay (48000, 256);
    render (a, b, 2); pump (a, b);
    check (controller.plan.valid(), "group analysis automatically builds a proposal");
    if (! controller.plan.valid()) std::printf ("Plan error: %s\n", controller.status.toRawUTF8());
    const auto beforeA = a.assistSettings(), beforeB = b.assistSettings();
    controller.audition (true); pump (a, b);
    check (controller.mode == MixController::Mode::proposed && ! controller.busy(), "proposal applied and acknowledged");
    check (a.assistSettings().enabled && b.assistSettings().enabled, "both track processors enabled");
    const auto proposedB = b.assistSettings();
    check (proposedB.presence < 0, "vocal masking EQ reaches backing processor");
    controller.audition (false); pump (a, b);
    auto originalA = a.assistSettings(); originalA.trim = 0;
    check (equivalent (originalA, beforeA), "original audition restores baseline parameters");
    check (b.assistSettings().trim <= 0, "comparison never boosts monitoring");
    controller.audition (true); pump (a, b); controller.keep(); pump (a, b);
    check (controller.mode == MixController::Mode::kept && std::abs (b.assistSettings().trim) < 1.0e-7f, "keep removes temporary matching trim");

    juce::MemoryBlock state; b.getStateInformation (state);
    SattariMixAudioProcessor restored; restored.setStateInformation (state.getData(), (int) state.getSize());
    check (equivalent (restored.assistSettings(), b.assistSettings()), "saved assistant sound recalls without editor");
    check (restored.groupName() == group && restored.instanceId() != b.instanceId(), "recall preserves group but clone identity is unique");

    if (screenshots != juce::File())
    {
        screenshots.createDirectory();
        std::unique_ptr<juce::AudioProcessorEditor> editor (a.createEditor());
        for (auto size : { juce::Point<int> (1000, 760), juce::Point<int> (1180, 820), juce::Point<int> (1600, 1100) })
        {
            editor->setSize (size.x, size.y);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            auto stream = screenshots.getChildFile ("automix-" + juce::String (size.x) + ".png").createOutputStream();
            if (stream != nullptr) { stream->setPosition (0); stream->truncate(); }
            check (stream != nullptr && juce::PNGImageFormat().writeImageToStream (image, *stream), "editor screenshot renders");
        }
    }

    controller.undo(); pump (a, b);
    check (equivalent (a.assistSettings(), beforeA) && equivalent (b.assistSettings(), beforeB), "undo restores entire group");
    controller.buildProposal();
    auto changed = b.assistSettings(); changed.gain = 3; b.setAssistSettings (changed); pump (a, b);
    controller.audition (true);
    check (! controller.busy() && ! a.assistSettings().enabled, "stale proposal cannot overwrite manual gain");

    b.setAssistSettings (beforeB); pump (a, b); controller.buildProposal(); controller.audition (true);
    setRealParameter (b.parameters(), "auto.lock", 1); pump (a, b); pump (a, b);
    check (equivalent (a.assistSettings(), beforeA), "mid-apply lock failure rolls back other track");
    setRealParameter (b.parameters(), "slot1", 4);
    pump (a, b); controller.buildProposal();
    check (! controller.plan.valid(), "changed rack processing invalidates captured measurements");

    // A legacy host state must not retain a previously enabled assistant.
    auto legacy = b.parameters().copyState();
    for (int i = legacy.getNumChildren(); --i >= 0;)
        if (legacy.getChild (i).getProperty ("id").toString().startsWith ("auto.")) legacy.removeChild (i, nullptr);
    juce::MemoryBlock legacyData;
    juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), legacyData);
    auto active = b.assistSettings(); active.enabled = true; active.gain = 9; b.setAssistSettings (active);
    b.setStateInformation (legacyData.getData(), (int) legacyData.getSize());
    std::printf ("Legacy restore: enabled=%d gain=%.9g\n", b.assistSettings().enabled ? 1 : 0, (double) b.assistSettings().gain);
    check (! b.assistSettings().enabled && std::abs (b.assistSettings().gain) < 1.0e-4f, "legacy state loads assistant defaults");
    const auto& params = a.getParameters();
    auto* lastOld = a.parameters().getParameter ("mod4.phase"); auto* firstNew = a.parameters().getParameter ("auto.gain");
    check (params.indexOf (firstNew) == params.indexOf (lastOld) + 1, "new host parameters append after original parameter set");
}

static void dspContract()
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 64, 511 })
        {
            AssistDSP dsp; dsp.prepare (sr, block); Settings s;
            juce::AudioBuffer<float> audio (2, block), reference (2, block);
            fill (audio, 0, sr, 0.1f, 997); reference.makeCopyOf (audio); dsp.process (audio, s);
            check (identical (audio, reference), "default assistant is bit-transparent");
            s.enabled = true; s.gain = -6; dsp.prime (s);
            fill (audio, 0, sr, 0.1f, 997); dsp.process (audio, s);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i)
                check (std::abs (audio.getSample (c, i) - reference.getSample (c, i) * (float) amplitude (-6)) < 1.0e-6f,
                       "gain render matches requested gain");
            s.amount = 0; dsp.prime (s); fill (audio, 0, sr, 0.1f, 997); dsp.process (audio, s);
            check (identical (audio, reference), "zero amount is bit-transparent");
        }
    SattariMixAudioProcessor processor;
    auto buses = processor.getBusesLayout(); buses.inputBuses.set (1, juce::AudioChannelSet::stereo());
    check (processor.setBusesLayout (buses), "external sidechain layout is accepted");
    auto s = processor.assistSettings(); s.enabled = true; s.gain = -6; s.low = -2; s.presence = -2; s.compression = 0.5f;
    processor.setAssistSettings (s); processor.prepareToPlay (48000, 511);
    juce::AudioBuffer<float> audio (4, 511), reference (4, 511); juce::MidiBuffer midi;
    fill (audio, 123, 48000, 0.2f, 997); reference.makeCopyOf (audio); processor.processBlock (audio, midi);
    for (int c = 2; c < 4; ++c)
        check (std::memcmp (audio.getReadPointer (c), reference.getReadPointer (c), 511 * sizeof (float)) == 0, "assistant leaves sidechain samples untouched");
}

static int peer (const juce::String& code)
{
    Session session; if (! session.join (code)) return 3;
    auto t = fixture (session.id, Role::instrument, -24, -12); t.name = "external process";
    const double until = juce::Time::getMillisecondCounterHiRes() + 60000;
    while (juce::Time::getMillisecondCounterHiRes() < until)
    {
        const auto c = session.receive();
        if (get (c, "action").toString() == "exit") return 0;
        t.ack = get (c, "token").toString();
        session.publish (t); juce::Thread::sleep (20);
    }
    return 4;
}

static void processContract()
{
    const auto group = "process-contract-" + juce::Uuid().toString();
    Session local; local.join (group);
    juce::ChildProcess child;
    check (child.start (juce::StringArray { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), "--peer", group }), "launch isolated peer");
    std::vector<Track> peers;
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000;
    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    { peers = local.tracks(); if (! peers.empty()) break; juce::Thread::sleep (20); }
    check (peers.size() == 1, "cross-process telemetry discovery");
    if (! peers.empty())
    {
        const auto peerId = peers[0].id;
        auto c = object(); put (c, "action", "test"); put (c, "token", "test-token"); local.send (peerId, c);
        bool ack = false;
        for (int i = 0; i < 100; ++i)
        { peers = local.tracks(); if (! peers.empty() && peers[0].ack == "test-token") { ack = true; break; } juce::Thread::sleep (20); }
        check (ack, "cross-process command acknowledgement");
        Session isolated; isolated.join (group + "-other"); check (isolated.tracks().empty(), "separate project groups are isolated");
        put (c, "action", "exit"); local.send (peerId, c);
    }
    check (child.waitForProcessToFinish (3000) && child.getExitCode() == 0, "peer exits cleanly");
    if (child.isRunning()) child.kill();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--peer") return peer (argv[2]);
    analyzerContract(); plannerContract(); dspContract();
    workflowContract (argc >= 2 ? juce::File (argv[1]) : juce::File());
    processContract();
    std::printf ("Auto Mix contract: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
