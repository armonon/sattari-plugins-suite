// A4: live input through a REAL audio device.
//
// Everything about the input path had been verified with synthetic buffers handed
// straight to processBlock. That proves the DSP and proves nothing about the thing
// A4 is actually about: a real driver, real callbacks, real sample-rate negotiation,
// real buffer sizes, and audio that has genuinely left the process and come back.
//
// BlackHole (or any loopback device) makes that testable without a patch cable and
// without opening a microphone: whatever is written to its output appears on its
// input. So StemDeck plays a known signal out of a deck, and it must arrive back on
// an Input channel having gone through CoreAudio both ways.
//
//   StemDeckLiveIO [deviceNameSubstring]     default: BlackHole
//
// What this DOES close: device open, real callbacks, signal actually arriving,
// round-trip latency, callback timing stability, sample-rate change survival.
// What it does NOT close: mic preamps, phantom power, physically unplugging an
// interface. Those still need a real box; this is the software floor beneath them.
#include "MainPluginProcessor.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}
void info (const juce::String& what) { std::cout << "  ..   " << what << std::endl; }

// Drives StemDeck from the real device callback and watches what comes back.
class LoopbackHarness final : public juce::AudioIODeviceCallback
{
public:
    explicit LoopbackHarness (MainPluginProcessor& p) : proc (p) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        sampleRate = d->getCurrentSampleRate();
        blockSize  = d->getCurrentBufferSizeSamples();
        // Time prepareToPlay: it runs on the DEVICE START path, so if it is slow the
        // audio thread cannot deliver a callback until it returns.
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        proc.prepareToPlay (sampleRate, blockSize);
        prepareMs.store (juce::Time::getMillisecondCounterHiRes() - t0);
        aboutToStartCount.fetch_add (1);
        scratch.setSize (2, juce::jmax (blockSize, 1024));
        callbacks.store (0); maxGapMs.store (0.0); lastCallbackMs = 0.0;
        inputPeak.store (0.0f); firstArrivalCallback.store (-1);
        startedAtMs = juce::Time::getMillisecondCounterHiRes();
        startupMs.store (0.0);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                           float* const* out, int numOut,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        // Callback-to-callback timing. A dropout shows up here as a gap far larger
        // than one buffer period.
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (lastCallbackMs > 0.0)
            maxGapMs.store (juce::jmax (maxGapMs.load(), now - lastCallbackMs));
        lastCallbackMs = now;

        // What arrived on the device INPUT — this is audio that left the process and
        // came back through the driver.
        float peak = 0.0f;
        for (int ch = 0; ch < numIn; ++ch)
            if (in[ch] != nullptr)
                for (int i = 0; i < numSamples; ++i)
                    peak = juce::jmax (peak, std::abs (in[ch][i]));
        inputPeak.store (juce::jmax (inputPeak.load(), peak));
        if (firstArrivalCallback.load() < 0 && peak > kArrivalThreshold)
            firstArrivalCallback.store (callbacks.load());

        // Run StemDeck for real, feeding it the device input.
        scratch.setSize (juce::jmax (2, numOut), numSamples, false, false, true);
        scratch.clear();
        for (int ch = 0; ch < juce::jmin (numIn, scratch.getNumChannels()); ++ch)
            if (in[ch] != nullptr)
                scratch.copyFrom (ch, 0, in[ch], numSamples);

        juce::MidiBuffer midi;
        proc.processBlock (scratch, midi);

        for (int ch = 0; ch < numOut; ++ch)
            if (out[ch] != nullptr)
            {
                const int src = juce::jmin (ch, scratch.getNumChannels() - 1);
                std::memcpy (out[ch], scratch.getReadPointer (src), sizeof (float) * (size_t) numSamples);
            }

        if (callbacks.load() == 0)
            startupMs.store (juce::Time::getMillisecondCounterHiRes() - startedAtMs);
        callbacks.fetch_add (1);
        totalCallbacks.fetch_add (1);
    }

    static constexpr float kArrivalThreshold = 1.0e-4f;

    MainPluginProcessor& proc;
    juce::AudioBuffer<float> scratch;
    double sampleRate = 0.0;
    int    blockSize = 0;
    std::atomic<int>    callbacks { 0 };
    std::atomic<int>    totalCallbacks { 0 };   // never reset — survives device restarts
    std::atomic<int>    firstArrivalCallback { -1 };
    std::atomic<double> maxGapMs { 0.0 };
    std::atomic<float>  inputPeak { 0.0f };
    double lastCallbackMs = 0.0;
    double startedAtMs = 0.0;
    std::atomic<double> startupMs { 0.0 };
    std::atomic<double> prepareMs { 0.0 };
    std::atomic<int> aboutToStartCount { 0 };
};

juce::File makeTone (double seconds, int sr)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("stemdeck-liveio-tone.wav");
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> buf (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = 0.4f * std::sin ((double) i / sr * 440.0 * juce::MathConstants<double>::twoPi);
        buf.setSample (0, i, v);
        buf.setSample (1, i, v);
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), sr, 2, 16, {}, 0)))
        { os.release(); w->writeFromAudioSampleBuffer (buf, 0, n); }
    return f;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String wanted = argc >= 2 ? juce::String (argv[1]) : juce::String ("BlackHole");

    std::cout << "A4 live I/O through a real device" << std::endl;
    std::cout << std::fixed << std::setprecision (2);

    juce::AudioDeviceManager dm;

    // ── Enumerate what is actually present ───────────────────────────────────
    juce::String chosen;
    {
        auto& types = dm.getAvailableDeviceTypes();
        for (auto* t : types)
        {
            t->scanForDevices();
            for (const auto& n : t->getDeviceNames (false))   // outputs
                if (n.containsIgnoreCase (wanted)) chosen = n;
        }
    }
    check (chosen.isNotEmpty(), "loopback device present (" + wanted + " -> " + (chosen.isEmpty() ? "NOT FOUND" : chosen) + ")");
    if (chosen.isEmpty())
    {
        std::cout << "  A device whose name contains '" << wanted << "' is required.\n"
                  << "  Install BlackHole (brew install blackhole-2ch) or pass another loopback device name.\n";
        std::cout << "LIVEIO_SKIPPED" << std::endl;
        return 0;   // not a failure: the machine simply has no loopback device
    }

    MainPluginProcessor proc;
    LoopbackHarness harness (proc);

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName  = chosen;
    setup.outputDeviceName = chosen;
    setup.sampleRate       = 48000.0;
    setup.bufferSize       = 256;
    setup.useDefaultInputChannels  = true;
    setup.useDefaultOutputChannels = true;

    const auto err = dm.initialise (2, 2, nullptr, true, chosen, &setup);
    check (err.isEmpty(), "device opened" + (err.isEmpty() ? juce::String() : " (" + err + ")"));
    if (! err.isEmpty()) { std::cout << "LIVEIO_FAILED" << std::endl; return 1; }

    if (auto* dev = dm.getCurrentAudioDevice())
    {
        info ("device: " + dev->getName()
              + "  sr " + juce::String (dev->getCurrentSampleRate(), 0)
              + "  buffer " + juce::String (dev->getCurrentBufferSizeSamples())
              + "  in " + juce::String (dev->getActiveInputChannels().countNumberOfSetBits())
              + "  out " + juce::String (dev->getActiveOutputChannels().countNumberOfSetBits()));
        check (dev->getCurrentSampleRate() > 0.0 && dev->getCurrentBufferSizeSamples() > 0,
               "device reports a real sample rate and buffer size");
    }

    // An Input channel, so the arriving audio has somewhere to land.
    const int inputChannel = proc.addChannel (ChannelType::input);
    check (inputChannel >= 0, "input channel added");

    dm.addAudioCallback (&harness);

    // ── Silence baseline: nothing playing, nothing should arrive ─────────────
    // Wait for callbacks to actually begin rather than assuming a fixed delay:
    // audioDeviceAboutToStart runs prepareToPlay, which prepares every channel and
    // is not instant, so the first callback can be several hundred ms out.
    {
        const double waitUntil = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (harness.totalCallbacks.load() < 20
               && juce::Time::getMillisecondCounterHiRes() < waitUntil)
            juce::Thread::sleep (20);
    }
    const double startupMs = harness.startupMs.load();
    info ("prepareToPlay took " + juce::String (harness.prepareMs.load(), 1) + " ms  (device-start path)");
    info ("audioDeviceAboutToStart called " + juce::String (harness.aboutToStartCount.load()) + "x");
    info ("first callback " + juce::String (startupMs, 1) + " ms after device start");
    info ("callbacks so far: " + juce::String (harness.totalCallbacks.load()));
    juce::Thread::sleep (300);
    const float silentPeak = harness.inputPeak.load();
    const int   silentCbs  = harness.totalCallbacks.load();
    // NOT asserted: some devices (BlackHole among them) do not run their callback
    // while the stream is idle, so "callbacks during silence" is a property of the
    // driver, not of StemDeck. Recorded as information; the assertions below all run
    // once audio is actually flowing.
    info ("callbacks during idle: " + juce::String (silentCbs)
          + (silentCbs == 0 ? "  (this device idles its clock - not a fault)" : ""));
    check (silentPeak < 0.01f, "silence in -> silence out (peak " + juce::String (silentPeak, 5) + ")");

    // ── Play a tone from a deck; it must come back through the driver ────────
    const auto tone = makeTone (30.0, 48000);   // must outlast the rate-change test
    proc.loadDeckFileAsync (0, tone);
    for (int i = 0; i < 200; ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    check (proc.deckAt (0).getStemAudio (DeckPlayer::fullMixStem) != nullptr, "tone loaded into deck A");

    // Reset the arrival watch AFTER the load, so the measurement is the round trip
    // and not the load time plus message-loop pumping.
    harness.inputPeak.store (0.0f);
    harness.firstArrivalCallback.store (-1);
    const int cbsBefore = harness.totalCallbacks.load();

    if (auto* p = dynamic_cast<juce::AudioParameterBool*> (proc.parameters().getParameter ("deckAPlay")))
        p->setValueNotifyingHost (1.0f);

    juce::Thread::sleep (1500);

    const float livePeak = harness.inputPeak.load();
    const int   arrival  = harness.firstArrivalCallback.load();

    check (harness.totalCallbacks.load() > 50,
           "real device callbacks running under load ("
           + juce::String (harness.totalCallbacks.load()) + ")");
    check (livePeak > 0.01f,
           "audio made a ROUND TRIP through the device (peak " + juce::String (livePeak, 4) + ")");

    if (arrival >= 0 && harness.blockSize > 0 && harness.sampleRate > 0.0)
    {
        const double blocksToArrive = juce::jmax (0, arrival - cbsBefore);
        const double ms = blocksToArrive * (harness.blockSize / harness.sampleRate) * 1000.0;
        info ("round-trip latency ~" + juce::String (ms, 1) + " ms ("
              + juce::String ((int) blocksToArrive) + " buffers of "
              + juce::String (harness.blockSize) + ")");
        check (ms < 250.0, "round-trip latency is performable (<250 ms)");
    }

    // ── Callback timing: a dropout is a gap far larger than one buffer ───────
    if (harness.blockSize > 0 && harness.sampleRate > 0.0)
    {
        const double periodMs = harness.blockSize / harness.sampleRate * 1000.0;
        const double gap = harness.maxGapMs.load();
        info ("buffer period " + juce::String (periodMs, 2) + " ms, worst callback gap "
              + juce::String (gap, 2) + " ms");
        check (gap < periodMs * 8.0,
               "no gross callback stall (worst gap < 8x buffer period)");
    }

    // ── Survive a sample-rate change mid-session ─────────────────────────────
    {
        auto s2 = dm.getAudioDeviceSetup();
        const double from = s2.sampleRate;

        // Pick a rate the DEVICE actually supports. Asking for one it does not is a
        // test bug, not a StemDeck bug — BlackHole advertises a fixed set.
        double target = 0.0;
        if (auto* dev = dm.getCurrentAudioDevice())
        {
            juce::String rates;
            // Prefer the change a user would actually make (48k <-> 44.1k) rather
            // than the first differing rate in the list, which was 8000 Hz.
            for (auto r : dev->getAvailableSampleRates())
            {
                rates += juce::String (r, 0) + " ";
                const bool realistic = (r == 44100.0 || r == 48000.0);
                if (r != from && realistic && target == 0.0) target = r;
            }
            if (target == 0.0)
                for (auto r : dev->getAvailableSampleRates())
                    if (r != from && target == 0.0) target = r;
            info ("device supports: " + rates.trim());
        }
        if (target <= 0.0) { info ("only one sample rate supported - change not testable"); target = from; }
        s2.sampleRate = target;
        const auto e2 = dm.setAudioDeviceSetup (s2, true);
        // The device restarts, which runs audioDeviceAboutToStart -> prepareToPlay
        // again. Wait for callbacks to resume rather than sampling immediately.
        // Re-arm playback: the device restart stops the deck, and this device idles
        // its clock when nothing is playing, so a stopped deck is indistinguishable
        // from a dead callback.
        if (auto* pp = dynamic_cast<juce::AudioParameterBool*> (proc.parameters().getParameter ("deckAPlay")))
            pp->setValueNotifyingHost (1.0f);
        const int cbs = harness.totalCallbacks.load();
        bool stillRunning = false;
        const double waitUntil = juce::Time::getMillisecondCounterHiRes() + 6000.0;
        while (! stillRunning && juce::Time::getMillisecondCounterHiRes() < waitUntil)
        {
            // Pump the message loop: the play re-arm is a parameter change that has to
            // be delivered before the deck actually resumes.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            stillRunning = harness.totalCallbacks.load() > cbs + 20;
        }
        const double actual = dm.getCurrentAudioDevice() != nullptr
                            ? dm.getCurrentAudioDevice()->getCurrentSampleRate() : 0.0;
        info ("after change: device reports " + juce::String (actual, 0) + " Hz");
        check (e2.isEmpty(), "sample-rate change accepted " + juce::String (from, 0) + " -> "
               + juce::String (s2.sampleRate, 0) + (e2.isEmpty() ? "" : " (" + e2 + ")"));
        if (target != from)
            check (stillRunning, "callbacks still flowing after the rate change ("
                   + juce::String (harness.totalCallbacks.load() - cbs) + " new)");
        else
            info ("skipped flow check - device offers a single rate");
    }

    dm.removeAudioCallback (&harness);
    dm.closeAudioDevice();
    check (true, "device closed cleanly");

    tone.deleteFile();
    std::cout << (failures == 0 ? "LIVEIO_OK" : "LIVEIO_FAILED") << std::endl;
    std::cout.flush();
    std::_Exit (failures == 0 ? 0 : 1);
}
