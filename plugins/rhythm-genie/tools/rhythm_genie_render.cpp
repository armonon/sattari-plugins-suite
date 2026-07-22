// Sattari Rhythm Genie — offline MIDI-generation proof.
//
// Drives the processor with a mock playhead at 120 BPM and collects the MIDI it
// emits, then asserts the "Four on Floor" kick lands on the four downbeats
// (PPQ 0,1,2,3) and the snare on the backbeats (PPQ 1,3). Proves the host-synced
// step engine actually places notes at the right musical positions.
//
//   rhythm_genie_render          # drive + assert the grid timing (local proof)
//   rhythm_genie_render --smoke  # assert some notes are emitted (CI-safe)
#include "RhythmGenieProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    else std::fprintf (stderr, "WARNING: missing parameter %s\n", id.toRawUTF8());
}

struct MockPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        return info;
    }
};

// Peak absolute sample across a buffer — a cheap "did it make sound" probe.
float bufferPeak (const juce::AudioBuffer<float>& b)
{
    float pk = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        pk = std::max (pk, b.getMagnitude (ch, 0, b.getNumSamples()));
    return pk;
}

// Loop proof: with the sequencer silenced, play ONE live pad hit in cycle 1 and prove the
// looper repeats it (a hit at the same phase in cycle 2, with none in between).
int runLoopTest()
{
    const double sr = 48000.0; const int block = 256; const double bpm = 120.0;
    SattariRhythmGenieAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "loopOn",  1.0f);
    setParam (apvts, "loopBars", 0.0f);   // choice index 0 = 1 bar = 4 beats
    setParam (apvts, "kickOn",  0.0f);    // silence the step sequencer entirely
    setParam (apvts, "snareOn", 0.0f);
    setParam (apvts, "chatOn",  0.0f);
    setParam (apvts, "ohatOn",  0.0f);

    MockPlayHead mph; mph.bpm = bpm; mph.playing = true;
    proc.setPlayHead (&mph);
    proc.prepareToPlay (sr, block);

    const double spb = sr * 60.0 / bpm;
    const double blockBeats = block / spb;
    const double injectPpq = 1.0;         // phase 0.25 within the 4-beat loop

    float cyc1 = 0.0f, cyc2replay = 0.0f, cyc2quiet = 0.0f;
    double ppq = 0.0; bool injected = false;
    while (ppq < 8.2)                      // two full 1-bar cycles + a hair
    {
        mph.ppq = ppq;
        juce::AudioBuffer<float> audio (2, block); audio.clear();
        juce::MidiBuffer midi;
        if (! injected && ppq <= injectPpq && injectPpq < ppq + blockBeats)
        {
            const int off = (int) ((injectPpq - ppq) * spb);
            midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120), juce::jlimit (0, block - 1, off));
            injected = true;
        }
        proc.processBlock (audio, midi);
        const float pk = bufferPeak (audio);
        if (ppq >= 1.0 && ppq < 2.0) cyc1       = std::max (cyc1,       pk);   // the live hit
        if (ppq >= 5.0 && ppq < 5.7) cyc2replay = std::max (cyc2replay, pk);   // loop replay
        if (ppq >= 6.5 && ppq < 7.5) cyc2quiet  = std::max (cyc2quiet,  pk);   // off-phase = silent
        ppq += blockBeats;
    }

    std::printf ("Rhythm Genie loop: cyc1-hit=%.3f, cyc2-replay=%.3f, cyc2-quiet=%.4f\n",
                 cyc1, cyc2replay, cyc2quiet);
    if (cyc1 < 1.0e-2f)                 { std::fprintf (stderr, "FAIL: live loop hit made no sound\n"); return 1; }
    if (cyc2replay < 1.0e-2f)           { std::fprintf (stderr, "FAIL: loop did not replay in cycle 2\n"); return 1; }
    if (cyc2replay < cyc2quiet * 2.0f)  { std::fprintf (stderr, "FAIL: loop replay not localised to its phase\n"); return 1; }
    return 0;
}

// Editable-grid proof: an empty grid is silent; toggling cells makes exactly those lanes
// fire — including lane 4 (Clap = GM 39), proving the 8-lane grid drives the output.
int runGridTest()
{
    const double sr = 48000.0; const int block = 512; const double bpm = 120.0;
    SattariRhythmGenieAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "swing", 0.0f); setParam (apvts, "humanize", 0.0f);
    setParam (apvts, "kickOn", 1.0f); setParam (apvts, "snareOn", 1.0f);
    setParam (apvts, "chatOn", 1.0f); setParam (apvts, "ohatOn", 1.0f);

    MockPlayHead mph; mph.bpm = bpm; mph.playing = true;
    proc.setPlayHead (&mph);
    proc.prepareToPlay (sr, block);
    const double spb = sr * 60.0 / bpm;
    const double bb  = block / spb;

    // 1) Cleared grid = no notes across a bar.
    proc.clearGrid();
    int cleared = 0;
    for (double ppq = 0.0; ppq < 4.0; ppq += bb)
    {
        mph.ppq = ppq;
        juce::AudioBuffer<float> a (2, block); a.clear();
        juce::MidiBuffer m; proc.processBlock (a, m);
        for (const auto meta : m) if (meta.getMessage().isNoteOn()) ++cleared;
    }

    // 2) Toggle kick (lane 0) and clap (lane 4) on step 0 — both must fire their GM notes.
    proc.toggleCell (0, 0);
    proc.toggleCell (4, 0);
    bool sawKick = false, sawClap = false; int edited = 0;
    for (double ppq = 0.0; ppq < 1.0; ppq += bb)
    {
        mph.ppq = ppq;
        juce::AudioBuffer<float> a (2, block); a.clear();
        juce::MidiBuffer m; proc.processBlock (a, m);
        for (const auto meta : m)
        {
            const auto msg = meta.getMessage();
            if (! msg.isNoteOn()) continue;
            ++edited;
            if (msg.getNoteNumber() == 36) sawKick = true;   // GM kick
            if (msg.getNoteNumber() == 39) sawClap = true;   // GM clap (lane 4)
        }
    }

    std::printf ("Rhythm Genie grid: cleared-notes=%d, edited kick=%d clap=%d\n", cleared, (int) sawKick, (int) sawClap);
    if (cleared != 0) { std::fprintf (stderr, "FAIL: cleared grid still emitted %d note(s)\n", cleared); return 1; }
    if (! sawKick)    { std::fprintf (stderr, "FAIL: toggled kick cell (lane 0) did not fire GM 36\n"); return 1; }
    if (! sawClap)    { std::fprintf (stderr, "FAIL: toggled clap cell (lane 4) did not fire GM 39\n"); return 1; }
    return 0;
}

// Count note-ons for a given note whose PPQ position lands near an integer beat.
int onBeatCount (const std::vector<std::pair<double,int>>& notes, int note, double maxBeats)
{
    int c = 0;
    for (const auto& e : notes)
        if (e.second == note && e.first < maxBeats)
        {
            const double frac = std::abs (e.first - std::round (e.first));
            if (frac < 0.06) ++c;     // within ~6% of a beat
        }
    return c;
}
}

int main (int argc, char** argv)
{
    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);
    const double sr = 48000.0;
    const int    block = 512;
    const double bpm = 120.0;

    SattariRhythmGenieAudioProcessor proc;
    auto& apvts = proc.parameters();
    setParam (apvts, "style", 0.0f);       // Four on Floor
    setParam (apvts, "density", 0.5f);
    setParam (apvts, "swing", 0.0f);
    setParam (apvts, "velocity", 0.8f);
    setParam (apvts, "gate", 0.5f);
    setParam (apvts, "humanize", 0.0f);
    setParam (apvts, "kickOn", 1.0f);
    setParam (apvts, "snareOn", 1.0f);
    setParam (apvts, "chatOn", 1.0f);
    setParam (apvts, "ohatOn", 1.0f);

    MockPlayHead mph; mph.bpm = bpm;
    proc.setPlayHead (&mph);
    proc.prepareToPlay (sr, block);

    const double spb = sr * 60.0 / bpm;          // samples per beat
    const double blockBeats = block / spb;

    std::vector<std::pair<double,int>> notes;    // (ppqPosition, note)
    double ppq = 0.0;
    int onCount = 0;
    float playPeak = 0.0f;                        // loudest sample while the sequencer plays
    while (ppq < 4.2)                            // a bit over one 4/4 bar
    {
        mph.ppq = ppq;
        juce::AudioBuffer<float> audio (2, block); audio.clear();
        juce::MidiBuffer midi;
        proc.processBlock (audio, midi);
        playPeak = std::max (playPeak, bufferPeak (audio));
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                notes.emplace_back (ppq + (double) meta.samplePosition / spb, m.getNoteNumber());
                ++onCount;
            }
        }
        ppq += blockBeats;
    }

    if (onCount == 0) { std::fprintf (stderr, "FAIL: no MIDI generated\n"); return 1; }

    // The kit is an instrument now: the same steps that emit MIDI must also make sound.
    if (playPeak < 1.0e-3f) { std::fprintf (stderr, "FAIL: sequencer produced no audio (peak %.6f)\n", playPeak); return 1; }
    // ...and a full bar of hits must stay under 0 dBFS (soft-clipped bus), not blow the master.
    if (playPeak > 1.0f)    { std::fprintf (stderr, "FAIL: kit bus clips over 0 dBFS (peak %.6f)\n", playPeak); return 1; }

    // Live pad while STOPPED: an incoming MIDI note (GM kick = 36) must voice the kit even
    // with the transport stopped, and a ring-out tail must persist after the note.
    {
        mph.playing = false;
        float stoppedPeak = 0.0f, tailPeak = 0.0f;
        for (int blk = 0; blk < 20; ++blk)
        {
            juce::AudioBuffer<float> audio (2, block); audio.clear();
            juce::MidiBuffer midi;
            if (blk == 0) midi.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 110), 0);
            proc.processBlock (audio, midi);
            const float pk = bufferPeak (audio);
            stoppedPeak = std::max (stoppedPeak, pk);
            if (blk >= 1) tailPeak = std::max (tailPeak, pk);   // audio after the trigger block
        }
        if (stoppedPeak < 1.0e-3f) { std::fprintf (stderr, "FAIL: live pad silent while stopped (peak %.6f)\n", stoppedPeak); return 1; }
        if (tailPeak    < 1.0e-4f) { std::fprintf (stderr, "FAIL: drum voice did not ring out (tail %.6f)\n", tailPeak); return 1; }
        std::printf ("Rhythm Genie audio: sequencer peak=%.3f, live-pad peak=%.3f, tail=%.4f\n",
                     playPeak, stoppedPeak, tailPeak);
    }

    if (runLoopTest() != 0) return 1;   // seamless loop mode: capture + replay
    if (runGridTest() != 0) return 1;   // editable multi-lane grid: clear + toggle

    if (smoke) { std::printf ("Rhythm Genie render smoke ok (%d notes, audio peak %.3f)\n", onCount, playPeak); return 0; }

    const int kicks  = onBeatCount (notes, 36, 4.0);   // kick on 0,1,2,3
    const int snares = onBeatCount (notes, 38, 4.0);   // snare on 1,3
    std::printf ("Rhythm Genie: %d notes in bar 1 — kicks on-beat=%d, snares on-beat=%d\n",
                 onCount, kicks, snares);

    if (kicks < 4)  { std::fprintf (stderr, "FAIL: four-on-floor kick not on all 4 beats (%d)\n", kicks); return 1; }
    if (snares < 2) { std::fprintf (stderr, "FAIL: snare not on the backbeats (%d)\n", snares); return 1; }

    std::printf ("Rhythm Genie step-timing proof ok\n");
    return 0;
}
