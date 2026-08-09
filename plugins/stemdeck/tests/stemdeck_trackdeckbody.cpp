// Render test for TrackDeckBody (Step 3): proves the extracted per-deck render
// produces real, finite audio through the ChannelBody interface, in isolation from
// MainPluginProcessor. Loads a sine into the body's DeckPlayer, renders a few
// blocks via processInto, and checks the output is non-silent, finite, and that a
// stem-gain change is audible (the RTSS path responds).
//
//   StemDeckTrackDeckBody → prints TRACKDECKBODY_OK on success
#include "TrackDeckBody.h"
#include <iostream>
#include <cmath>

namespace
{
constexpr double sampleRate = 44100.0;

std::shared_ptr<const DeckPlayer::LoadedAudio> makeSine (double hz, double seconds)
{
    auto a = std::make_shared<DeckPlayer::LoadedAudio>();
    const int n = (int) std::round (sampleRate * seconds);
    a->audio.setSize (2, n);
    a->sourceSampleRate = sampleRate;
    a->path = "synthetic://trackdeckbody.wav";
    a->displayName = "trackdeckbody sine";
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate));
        a->audio.setSample (0, i, v);
        a->audio.setSample (1, i, v);
    }
    a->analysis = TrackAnalyzer::analyze (a->audio, a->sourceSampleRate);
    return a;
}

double rms (const juce::AudioBuffer<float>& b, int n)
{
    double s = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i) { const float x = b.getSample (ch, i); s += (double) x * x; }
    return std::sqrt (s / juce::jmax (1, b.getNumChannels() * n));
}

bool allFinite (const juce::AudioBuffer<float>& b, int n)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i) if (! std::isfinite (b.getSample (ch, i))) return false;
    return true;
}
}

int main()
{
    constexpr int block = 512, chans = 2;

    TrackDeckBody body;
    body.prepare (sampleRate, block, chans);
    body.deckPlayer().setLoadedAudio (makeSine (220.0, 4.0));

    body.params.play = true;                 // stem gains default to 100% (unity)
    body.params.volume = 0.0f;               // 0 dB trim
    body.params.fader  = 1.0f;

    BlockContext ctx;
    ctx.numSamples = block;
    ctx.leftBusGain = ctx.rightBusGain = 1.0f;
    juce::MidiBuffer midi; ctx.midi = &midi;
    ctx.transport.bpm = 120.0;

    juce::AudioBuffer<float> scratch (chans, block);
    juce::AudioBuffer<float> capturedSource (chans, block);
    int sourceBlocks = 0;
    body.onPreFaderCapture = [&] (juce::AudioBuffer<float>& source, int n)
    {
        capturedSource.makeCopyOf (source, true);
        if (n == block)
            ++sourceBlocks;
    };

    // Warm up a few blocks (grid build, RTSS settle), then measure.
    double lastRms = 0.0;
    for (int blk = 0; blk < 8; ++blk)
    {
        scratch.clear();
        body.processInto (scratch, ctx);
        if (! allFinite (scratch, block)) { std::cerr << "FAIL: non-finite output at block " << blk << "\n"; return 1; }
        lastRms = rms (scratch, block);
    }
    if (lastRms < 0.01) { std::cerr << "FAIL: near-silent output, rms=" << lastRms << "\n"; return 1; }
    if (sourceBlocks != 8 || rms (capturedSource, block) < 0.01)
    {
        std::cerr << "FAIL: pre-fader source tap did not capture aligned audio\n";
        return 1;
    }
    if (body.getRealtimeSeparationBlocksForTest() != 0)
    {
        std::cerr << "FAIL: unity stems ran the real-time separator unnecessarily\n";
        return 1;
    }

    // A closed fader must silence only the printed/live render. Transport and clean
    // source capture continue so reopening the fader resumes at the correct position.
    body.params.fader = 0.0f;
    const double beforeMutedBlock = body.deckPlayer().getPlayheadReferenceSamples();
    scratch.clear();
    capturedSource.clear();
    body.processInto (scratch, ctx);
    const double afterMutedBlock = body.deckPlayer().getPlayheadReferenceSamples();
    if (rms (scratch, block) > 1.0e-5)
    {
        std::cerr << "FAIL: zero fader leaked into printed render\n";
        return 1;
    }
    if (rms (capturedSource, block) < 0.01 || afterMutedBlock <= beforeMutedBlock)
    {
        std::cerr << "FAIL: zero fader stopped transport or source capture\n";
        return 1;
    }
    body.params.fader = 1.0f;

    // Muting all stems (0%) must drop the level toward silence — proves the stem
    // gains actually drive the RTSS remix, not just pass-through.
    for (auto& g : body.params.stemGainPercent) g = 0.0f;
    double mutedRms = 0.0;
    for (int blk = 0; blk < 4; ++blk) { scratch.clear(); body.processInto (scratch, ctx); mutedRms = rms (scratch, block); }
    if (mutedRms > lastRms * 0.5) { std::cerr << "FAIL: muting stems did not reduce level (rms " << mutedRms << " vs " << lastRms << ")\n"; return 1; }
    if (body.getRealtimeSeparationBlocksForTest() == 0)
    {
        std::cerr << "FAIL: adjusted stems did not engage real-time separation\n";
        return 1;
    }

    std::cout << "TRACKDECKBODY_OK  playing_rms=" << lastRms << "  muted_rms=" << mutedRms << "\n";
    return 0;
}
