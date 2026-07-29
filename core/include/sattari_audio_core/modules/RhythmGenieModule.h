#pragma once

#include <sattari_audio_core/SattariModule.h>
#include <sattari_audio_core/SattariDrumKit.h>
#include <sattari_audio_core/SattariDrumPads.h>
#include <sattari_audio_core/SattariDrumLooper.h>
#include <sattari_audio_core/SattariBeatGrid.h>
#include <sattari_audio_core/SattariGroove.h>
#include <sattari_audio_core/SattariRhythmPatterns.h>
#include <sattari_audio_core/SattariCrossPluginBus.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

// =============================================================================
//  RhythmGenieModule — Sattari Rhythm Genie's drum engine, extracted so the
//  standalone plugin and a Sattari Stack layer run the SAME kit.
//
//  Split along the realtime boundary, the Chop pattern:
//    * the SHELL keeps file loading, the AudioFormatManager and the session-long
//      retain of every loaded sample;
//    * this module owns everything the audio thread touches — the step clock, the
//      synth kit, the sample voices, the live-hit FIFO and the looper.
//  They meet at setLaneSampleSlot(), the sibling of Chop's setSource().
//
//  ONE BEHAVIOUR DOES NOT SURVIVE THE MOVE INTO A STACK LAYER, deliberately:
//  Rhythm Genie also EMITS MIDI (its sequencer can drive other instruments). A
//  layering host hands every layer its own filtered MIDI buffer and clears MIDI
//  afterwards, so a layer's MIDI output has nowhere to go. Inside Stack the
//  sequencer therefore drives its own kit only — which is the part a layer is for.
//  The standalone plugin keeps its MIDI out unchanged.
// =============================================================================

namespace sattari::dsp
{

class RhythmGenieModule final : public SattariModule
{
public:
    static constexpr int kKitLanes  = 8;   // 0 kick 1 snare 2 chat 3 ohat 4 clap 5 tom 6 rim 7 ride
    static constexpr int kGridLanes = 8;   // one per kit voice

    // Reference-counted sample data, so an in-flight voice keeps its buffer alive
    // even after the slot is swapped or cleared.
    struct SampleSlot : juce::ReferenceCountedObject
    {
        juce::AudioBuffer<float> data;
        double sourceSr = 44100.0;
        juce::String name;
        using Ptr = juce::ReferenceCountedObjectPtr<SampleSlot>;
    };

    const char* moduleId()    const noexcept override { return "rhythmgenie"; }
    const char* displayName() const noexcept override { return "Rhythm Genie"; }

    static juce::StringArray styleChoices()
    {
        int n = 0; const auto* b = rhythm::patterns (n);
        juce::StringArray out;
        for (int i = 0; i < n; ++i) out.add (b[i].name);
        return out;
    }

    void addParameters (ParamList& out, const juce::String& prefix) override
    {
        using FloatP  = juce::AudioParameterFloat;
        using ChoiceP = juce::AudioParameterChoice;
        using BoolP   = juce::AudioParameterBool;
            auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

        out.push_back (std::make_unique<ChoiceP> (juce::ParameterID { prefix + "style", 1 }, "Style", styleChoices(), 0));
        out.push_back (std::make_unique<FloatP>  (juce::ParameterID { prefix + "density",  1 }, "Density",  unit(), 0.5f));
        out.push_back (std::make_unique<FloatP>  (juce::ParameterID { prefix + "swing",    1 }, "Swing",    unit(), 0.0f));
        out.push_back (std::make_unique<FloatP>  (juce::ParameterID { prefix + "velocity", 1 }, "Velocity", unit(), 0.8f));
        out.push_back (std::make_unique<FloatP>  (juce::ParameterID { prefix + "gate",     1 }, "Gate",     unit(), 0.5f));
        out.push_back (std::make_unique<FloatP>  (juce::ParameterID { prefix + "accent",   1 }, "Accent",   unit(), 0.35f));
        out.push_back (std::make_unique<FloatP>  (juce::ParameterID { prefix + "humanize", 1 }, "Humanize", unit(), 0.2f));

        static const char* laneIds[rhythm::kNumLanes] = { "kickOn", "snareOn", "chatOn", "ohatOn" };
        for (int l = 0; l < rhythm::kNumLanes; ++l)
            out.push_back (std::make_unique<BoolP> (juce::ParameterID { laneIds[l], 1 },
                                                  juce::String (rhythm::laneName (l)) + " On", true));

        // Audio kit: master output + per-lane sound shaping (level / tune / decay) for the
        // four sequencer lanes. Rhythm Genie is now a drum instrument, not just a MIDI source.
        out.push_back (std::make_unique<FloatP> (juce::ParameterID { prefix + "kitLevel", 1 }, "Kit Level", unit(), 0.8f));
        static const char* base[rhythm::kNumLanes] = { "kick", "snare", "chat", "ohat" };
        for (int l = 0; l < rhythm::kNumLanes; ++l)
        {
            const juce::String nm (rhythm::laneName (l));
            out.push_back (std::make_unique<FloatP> (juce::ParameterID { base[l] + juce::String ("Level"), 1 }, nm + " Level", unit(), 0.8f));
            out.push_back (std::make_unique<FloatP> (juce::ParameterID { base[l] + juce::String ("Tune"),  1 }, nm + " Tune",  unit(), 0.5f));
            out.push_back (std::make_unique<FloatP> (juce::ParameterID { base[l] + juce::String ("Decay"), 1 }, nm + " Decay", unit(), 0.5f));
        }

        // Seamless loop: capture live pad hits and repeat them every N bars ("looper feel").
        out.push_back (std::make_unique<BoolP>   (juce::ParameterID { prefix + "loopOn", 1 }, "Loop", false));
        out.push_back (std::make_unique<ChoiceP> (juce::ParameterID { prefix + "loopBars", 1 }, "Loop Bars",
                                                juce::StringArray { "1", "2", "4" }, 1));
    }


    // Genre-flavoured factory presets. Each sets the style + feel; loading one (setCurrentProgram)
    // regenerates the grid so the pattern actually changes. Styles: 0 Four-on-Floor, 1 Boom Bap,
    // 2 Trap, 3 Pop, 4 Breakbeat, 5 Half-Time.

    void prepare (double sampleRate, int samplesPerBlock, int /*numChannels*/) override
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        internalPpq = 0.0;
        pendingOffCount = 0;
        pendingOffScratchCount = 0;
        wasGenerating = false;
        playStep.store (-1);

        // Point each kit lane at its voice, prepare, and size the mono render scratch.
        using DT = sattari::dsp::DrumType;
        static const DT laneVoice[kKitLanes] = { DT::Kick, DT::Snare, DT::ClosedHat, DT::OpenHat,
                                                 DT::Clap, DT::Tom, DT::Rim, DT::Ride };
        for (int l = 0; l < kKitLanes; ++l) kit.setLaneType (l, laneVoice[l]);
        kit.prepare (currentSampleRate);
        kit.reset();
        kitScratch.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
        kitHitCount = 0;
        looper.reset();
        loopClockBeats = 0.0;
        wasLoopOn = false;
        for (auto& sp : samplePlay) { sp.active = false; sp.pos = 0.0; }   // keep loaded samples, stop voices

        // Resolve the shaping-param pointers once (off the audio thread).
        static const char* base[rhythm::kNumLanes] = { "kick", "snare", "chat", "ohat" };
        for (int l = 0; l < rhythm::kNumLanes; ++l)
        {
            char id[24];
            std::snprintf (id, sizeof id, "%sLevel", base[l]);
            laneLevelP[l] = cachedParameterPointer (id);
            std::snprintf (id, sizeof id, "%sTune", base[l]);
            laneTuneP[l] = cachedParameterPointer (id);
            std::snprintf (id, sizeof id, "%sDecay", base[l]);
            laneDecayP[l] = cachedParameterPointer (id);
        }
        kitLevelP = cachedParameterPointer ("kitLevel");
    }

    bool laneEnabled (int lane) const noexcept
    {
        static const char* ids[rhythm::kNumLanes] = { "kickOn", "snareOn", "chatOn", "ohatOn" };
        if (lane < 0 || lane >= kGridLanes) return false;
        if (lane < rhythm::kNumLanes) return param (ids[lane]) > 0.5f;
        return true;   // lanes 4-7 (clap/tom/rim/ride) have no mute toggle; always active
    }

    static int kitLaneNote (int lane) noexcept
    {
        // GM drum map for the 8 kit voices (kick snare chat ohat clap tom rim ride).
        static const int notes[kGridLanes] = { 36, 38, 42, 46, 39, 45, 37, 51 };
        return (lane >= 0 && lane < kGridLanes) ? notes[lane] : 36;
    }

    // Fill the drum lanes (0-3) from the current style + density; hand-placed sounds on
    // lanes 4-7 survive (SattariGroove only touches 0-3). Called from the constructor and
    // the editor's Generate button.

    void regenerateGrid()
    {
        using GS = sattari::dsp::GrooveStyle;
        // Style order: Four on Floor / Boom Bap / Trap / Pop / Breakbeat / Half-Time.
        static const GS map[6] = { GS::FourFloor, GS::BoomBap, GS::Trap, GS::House, GS::Breakbeat, GS::HalfTime };
        const int styleIdx = juce::jlimit (0, 5, (int) std::round (param ("style")));
        grid.setLanes (kGridLanes);
        grid.setSteps (rhythm::kSteps);
        sattari::dsp::generate (grid, map[styleIdx], param ("density"), (std::uint32_t) (rng.nextInt() | 1));
    }

    // --- Sample slots ----------------------------------------------------------------

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();

        // --- Transport + loop phase (computed first so live hits can be time-stamped) ------
        // The shell pushes the transport in; a module has no getPlayHead().
        const auto& t = transport;
        const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
        pubBpm.store (bpm, std::memory_order_relaxed);   // shared to the cross-plugin tempo bus
        const double samplesPerBeat = currentSampleRate * 60.0 / bpm;
        const double samplesPerStep = samplesPerBeat / 4.0;
        const double blockBeats     = (double) numSamples / samplesPerBeat;
        // "Standalone" here means: no host timeline to follow, so free-run the
        // internal clock. A module cannot see wrapperType, and the honest test is
        // the one that actually matters — whether a usable PPQ arrived.
        const bool standalone = ! t.hasPpq;

        double startPpq = 0.0;
        bool generate = false;
        if (t.hasPpq) { startPpq = t.ppqPosition;  generate = t.isPlaying; }
        else          { startPpq = internalPpq;    generate = t.isPlaying || standalone; }

        // Loop mode: capture live pad hits and repeat them every N bars ("looper feel"). The
        // loop runs off the host PPQ when playing, else a free clock so it cycles when stopped.
        const bool loopOn   = param ("loopOn") > 0.5f;
        const int  barsIdx  = (int) std::round (param ("loopBars"));
        const int  loopBars = (barsIdx <= 0) ? 1 : (barsIdx == 1 ? 2 : 4);
        const double loopLenBeats = (double) loopBars * 4.0;
        if (loopOn && ! wasLoopOn) looper.clear();       // fresh capture each time it is armed
        wasLoopOn = loopOn;
        looper.setEnabled (loopOn);

        const double loopBase = (t.hasPpq && t.isPlaying) ? startPpq : loopClockBeats;
        const double loopFrom = wrap01 (loopBase / loopLenBeats);
        const double loopTo   = wrap01 ((loopBase + blockBeats) / loopLenBeats);
        const double dPhase   = (loopLenBeats > 0.0) ? (blockBeats / loopLenBeats) : 0.0;
        if (! (t.hasPpq && t.isPlaying)) loopClockBeats += blockBeats;   // free-run when stopped
        auto phaseAtOffset = [&] (int off) noexcept
        {
            const double frac = numSamples > 0 ? (double) off / (double) numSamples : 0.0;
            return wrap01 (loopFrom + frac * dPhase);
        };

        // Replay the loop BEFORE ingesting this block's live hits, so a hit recorded now isn't
        // also collected now (it plays once live, then from the next cycle onward).
        if (loopOn)
        {
            sattari::dsp::DrumLooper::Hit lh[64];
            const int n = looper.collect (loopFrom, loopTo, lh, 64);
            for (int i = 0; i < n; ++i)
            {
                double d = lh[i].phase - loopFrom; if (d < 0.0) d += 1.0;
                const int off = juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                              dPhase > 0.0 ? (int) (d / dPhase * (double) numSamples) : 0);
                queueKitHit (lh[i].pad, lh[i].velocity, off);
            }
        }

        // --- Live pads: every incoming MIDI note-on plays the internal kit, always — even when
        // the transport is stopped. When looping, the hit is also recorded into the cycle.
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isNoteOn()) continue;
            const int lane = padMap.handleNoteOn (msg.getNoteNumber());
            if (lane >= 0)
            {
                const int off = juce::jlimit (0, juce::jmax (0, numSamples - 1), (int) meta.samplePosition);
                queueKitHit (lane, msg.getFloatVelocity(), off);
                if (loopOn) looper.recordHit (lane, msg.getFloatVelocity(), phaseAtOffset (off));
            }
        }

        drainLiveHits (loopOn, loopFrom);   // editor pad clicks / computer-key hits (fire at block start)

        juce::MidiBuffer generated;

        if (! generate)
        {
            if (wasGenerating) allNotesOff (generated);  // release hanging notes on stop
            wasGenerating = false;
            playStep.store (-1);
            renderKit (buffer, numSamples);              // still voice live pads + loop + ring-out tails
            midi.swapWith (generated);
            return;
        }
        wasGenerating = true;

        const double endPpq     = startPpq + blockBeats;
        const double swingPpq   = (double) param ("swing") * 0.125;  // delay odd 16ths up to half a step

        // Emit every step whose (swung) onset lands inside this block.
        for (int k = (int) std::floor (startPpq * 4.0) - 1; ; ++k)
        {
            const double onset = (double) k * 0.25 + ((k & 1) ? swingPpq : 0.0);
            if (onset >= endPpq) break;
            if (onset < startPpq) continue;
            const int offset = juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                             (int) std::lround ((onset - startPpq) * samplesPerBeat));
            emitStep (((k % rhythm::kSteps) + rhythm::kSteps) % rhythm::kSteps, offset, generated, samplesPerStep);
        }

        // Resolve note-offs (existing + the ones just scheduled) against this block.
        pendingOffScratchCount = 0;
        for (int i = 0; i < pendingOffCount; ++i)
        {
            auto po = pendingOffs[(size_t) i];
            if (po.samplesRemaining < numSamples)
                generated.addEvent (juce::MidiMessage::noteOff (kDrumChannel, po.note), po.samplesRemaining);
            else
            {
                po.samplesRemaining -= numSamples;
                if (pendingOffScratchCount < kMaxPendingOffs)
                    pendingOffScratch[(size_t) pendingOffScratchCount++] = po;
            }
        }
        for (int i = 0; i < pendingOffScratchCount; ++i)
            pendingOffs[(size_t) i] = pendingOffScratch[(size_t) i];
        pendingOffCount = pendingOffScratchCount;

        renderKit (buffer, numSamples);   // synth the queued step + live-pad hits into audio
        midi.swapWith (generated);

        if (! t.hasPpq) internalPpq += blockBeats;
        playStep.store ((((int) std::floor (startPpq * 4.0)) % rhythm::kSteps + rhythm::kSteps) % rhythm::kSteps);
    }

    void linkModule (juce::AudioProcessorValueTreeState& apvts, const juce::String& prefix) override
    {
        state = &apvts;
        pfx   = prefix;
    }

    // Drum tails, the looper capture and the step clock all carry across a
    // transport jump.
    void reset() override
    {
        kit.reset();
        looper.clear();
        for (auto& s : samplePlay) { s.active = false; s.pos = 0.0; }
        kitHitCount = 0;
        pendingOffCount = 0;
        pendingOffScratchCount = 0;
        internalPpq = 0.0;
        loopClockBeats = 0.0;
        wasLoopOn = false;
        wasGenerating = false;
        liveWrite.store (0); liveRead.store (0);
        playStep.store (-1);
        std::fill (kitScratch.begin(), kitScratch.end(), 0.0f);
    }

    void setTransport (const sattari::host::TransportSnapshot& t) noexcept override { transport = t; }

    // MESSAGE THREAD ONLY. The shell decodes the file and hands the slot over; the
    // audio thread swaps to it under the spin lock.
    void setLaneSampleSlot (int lane, SampleSlot::Ptr slot) noexcept
    {
        if (! juce::isPositiveAndBelow (lane, kGridLanes)) return;
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        laneSample[lane] = std::move (slot);
    }

    bool laneHasSample (int lane) const noexcept
    {
        return juce::isPositiveAndBelow (lane, kGridLanes) && laneSample[lane] != nullptr;
    }

    juce::String laneSampleName (int lane) const
    {
        return laneHasSample (lane) ? laneSample[lane]->name : juce::String();
    }

    // --- Editor-facing state (all lock-free or message-thread) --------------
    int  currentStep() const noexcept { return playStep.load(); }
    const BeatGrid& beatGrid() const noexcept { return grid; }

    // MESSAGE-THREAD accessors for state save/restore. The shell owns persistence
    // (it owns the XML), but the state itself lives here now.
    BeatGrid&   editableGrid() noexcept { return grid; }
    DrumPadMap& pads() noexcept        { return padMap; }
    void clearGrid() noexcept { grid.clear(); }
    void toggleCell (int lane, int step) noexcept { grid.toggle (lane, step); }

    void pushLiveNote (int note, float velocity) noexcept    { pushLive (0, note, velocity); }
    void pushLiveKey  (int keyCode, float velocity) noexcept { pushLive (1, keyCode, velocity); }
    void pushLiveLane (int lane, float velocity) noexcept    { pushLive (2, lane, velocity); }

    void armLearn (int pad) noexcept { padMap.armLearn (pad); }
    void cancelLearn() noexcept      { padMap.cancelLearn(); }
    int  learningPad() const noexcept { return padMap.learning(); }
    int  padMidiNote (int pad) const noexcept { return padMap.midiNote (pad); }
    int  padKeyCode  (int pad) const noexcept { return padMap.keyCode (pad); }

    double publishedBpm()   const noexcept { return pubBpm.load(); }
    juce::int64 publishedPulse() const noexcept { return pubPulse.load(); }

private:
    static constexpr int kDrumChannel = 10;   // GM percussion channel
    static inline double wrap01 (double p) noexcept { p -= (double) (long long) p; if (p < 0.0) p += 1.0; return p; }

    void allNotesOff (juce::MidiBuffer& out)
    {
        for (int i = 0; i < pendingOffCount; ++i)
        {
            const auto& po = pendingOffs[(size_t) i];
            out.addEvent (juce::MidiMessage::noteOff (kDrumChannel, po.note), 0);
        }
        pendingOffCount = 0;
        pendingOffScratchCount = 0;
    }

    void emitStep (int stepIdx, int sampleOffset,
                                                     juce::MidiBuffer& out, double samplesPerStep)
    {
        const float vBase = param ("velocity");
        const float acc   = param ("accent");
        const float hum   = param ("humanize");
        const int   gateS = juce::jmax (1, (int) (param ("gate") * samplesPerStep));

        for (int lane = 0; lane < kGridLanes; ++lane)
        {
            // The editable grid is the source of truth: a cell fires (probability-gated) and
            // carries its own velocity, so hand-drawn / generated rhythm::patterns both work.
            if (! laneEnabled (lane) || ! grid.fires (lane, stepIdx, rng.nextFloat()))
                continue;

            const float cellV = grid.velocity (lane, stepIdx);           // per-cell dynamics
            float v = vBase * (0.6f + 0.5f * cellV);                     // knob = master level
            if (stepIdx % 4 == 0) v += acc * 0.3f;                       // downbeat accent
            v += (rng.nextFloat() * 2.0f - 1.0f) * 0.15f * hum;          // humanise
            v = juce::jlimit (0.0f, 1.0f, v);
            const int vel = juce::jlimit (1, 127, (int) std::round (v * 127.0f));
            const int note = kitLaneNote (lane);

            out.addEvent (juce::MidiMessage::noteOn (kDrumChannel, note, (juce::uint8) vel), sampleOffset);
            if (pendingOffCount < kMaxPendingOffs)
                pendingOffs[(size_t) pendingOffCount++] = { note, sampleOffset + gateS };
            else
                out.addEvent (juce::MidiMessage::noteOff (kDrumChannel, note), sampleOffset);
            pubPulse.fetch_add (1, std::memory_order_relaxed);   // transient pulse for the bus
            queueKitHit (lane, juce::jlimit (0.0f, 1.0f, v), sampleOffset);   // + play the synth kit
        }
    }

    void queueKitHit (int lane, float velocity, int sampleOffset) noexcept
    {
        if (kitHitCount < kMaxKitHits)
            kitHits[(size_t) kitHitCount++] = { lane, velocity, sampleOffset };
    }

    // --- Editor -> audio live triggering ---------------------------------------------

    void pushLive (int kind, int id, float velocity) noexcept
    {
        const int w = liveWrite.load (std::memory_order_relaxed);
        const int nextW = (w + 1) % kLiveFifoSize;
        if (nextW == liveRead.load (std::memory_order_acquire)) return;   // full: drop the gesture
        liveFifo[(size_t) w] = { kind, id, juce::jlimit (0.0f, 1.0f, velocity) };
        liveWrite.store (nextW, std::memory_order_release);
    }


    // Drain on the audio thread: pad-map binding (learn) resolves here, so a "Set" armed from
    // the editor captures whichever note/key the user next plays. When looping, live hits are
    // also recorded into the loop at `phase` so overdubs join the cycle.
    void drainLiveHits (bool recordToLoop, double phase) noexcept
    {
        int r = liveRead.load (std::memory_order_relaxed);
        const int w = liveWrite.load (std::memory_order_acquire);
        while (r != w)
        {
            const auto lh = liveFifo[(size_t) r];
            int lane = -1;
            switch (lh.kind)
            {
                case 0:  lane = padMap.handleNoteOn (lh.id); break;
                case 1:  lane = padMap.handleKey    (lh.id); break;
                default: lane = lh.id;                       break;   // direct pad-button hit
            }
            if (lane >= 0)
            {
                queueKitHit (lane, lh.velocity, 0);
                if (recordToLoop) looper.recordHit (lane, lh.velocity, phase);
            }
            r = (r + 1) % kLiveFifoSize;
        }
        liveRead.store (r, std::memory_order_release);
    }

    sattari::dsp::DrumVoiceParams laneVoiceParams (int lane) const noexcept
    {
        sattari::dsp::DrumVoiceParams vp;
        // Only the four sequencer lanes have shaping params; MIDI-only lanes 4..7 use defaults.
        // Pointers are resolved in prepare, so this is lock-free and allocation-free.
        if (lane >= 0 && lane < rhythm::kNumLanes && laneLevelP[lane] != nullptr)
        {
            vp.level = laneLevelP[lane]->load (std::memory_order_relaxed);
            vp.tune  = laneTuneP[lane]->load (std::memory_order_relaxed);
            vp.decay = laneDecayP[lane]->load (std::memory_order_relaxed);
        }
        return vp;
    }

    // Segmented, sample-accurate render: advance the kit to each hit's offset, trigger, then
    // finish the block. Runs every block (even stopped) so tails ring out and live pads sound.

    void renderKit (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        if ((int) kitScratch.size() < numSamples) kitScratch.assign ((size_t) numSamples, 0.0f);
        std::fill (kitScratch.begin(), kitScratch.begin() + numSamples, 0.0f);

        // Order hits by sample offset (small n, no allocation).
        std::sort (kitHits.begin(), kitHits.begin() + kitHitCount,
                   [] (const KitHit& a, const KitHit& b) { return a.offset < b.offset; });

        int pos = 0;
        for (int i = 0; i < kitHitCount; ++i)
        {
            const int off = juce::jlimit (0, numSamples, kitHits[(size_t) i].offset);
            if (off > pos) { renderKitSegment (kitScratch.data() + pos, off - pos); pos = off; }
            triggerLane (kitHits[(size_t) i].lane, kitHits[(size_t) i].velocity);
        }
        if (pos < numSamples) renderKitSegment (kitScratch.data() + pos, numSamples - pos);

        // Drive the summed kit through a gentle tanh so a full bar of simultaneous hits glues
        // and stays under 0 dBFS instead of clipping the host (a bare sum peaks ~+3 dB).
        const float kitLvl = kitLevelP != nullptr ? kitLevelP->load (std::memory_order_relaxed) : 0.8f;
        const float drive  = 0.35f + 1.05f * juce::jlimit (0.0f, 1.0f, kitLvl);   // 0.35..1.40
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* w = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i) w[i] = std::tanh (kitScratch[(size_t) i] * drive);
        }

        kitHitCount = 0;   // hits consumed for this block
    }

    // Trigger a lane: play its loaded sample if there is one (and the swap isn't mid-flight),
    // otherwise the synthesised voice.
    void triggerLane (int lane, float velocity) noexcept
    {
        {
            const juce::SpinLock::ScopedTryLockType sl (sampleLock);
            if (sl.isLocked() && lane >= 0 && lane < kGridLanes && laneSample[lane] != nullptr)
            {
                const auto vp = laneVoiceParams (lane);
                auto& sp = samplePlay[lane];
                sp.buf  = laneSample[lane];      // retain (atomic inc); old ref survives in retainedSamples
                sp.pos  = 0.0;
                sp.gain = juce::jlimit (0.0f, 1.0f, velocity) * (0.2f + 0.8f * vp.level);
                const double semis = (double) (vp.tune - 0.5f) * 24.0;   // +/- 1 octave
                sp.rate = (sp.buf->sourceSr / (currentSampleRate > 0.0 ? currentSampleRate : 44100.0))
                          * std::pow (2.0, semis / 12.0);
                sp.active = true;
                return;
            }
        }
        kit.trigger (lane, velocity, laneVoiceParams (lane));
    }

    // Render one segment: the synth kit (additive) plus any active sample voices.
    void renderKitSegment (float* out, int numSamples) noexcept
    {
        kit.render (out, numSamples);
        for (int lane = 0; lane < kGridLanes; ++lane)
        {
            auto& sp = samplePlay[lane];
            if (! sp.active || sp.buf == nullptr) continue;
            const auto& d = sp.buf->data;
            const int len = d.getNumSamples();
            if (len < 2) { sp.active = false; continue; }
            const float* L = d.getReadPointer (0);
            const float* R = d.getNumChannels() > 1 ? d.getReadPointer (1) : L;
            for (int i = 0; i < numSamples; ++i)
            {
                const int idx = (int) sp.pos;
                if (idx >= len - 1) { sp.active = false; break; }
                const float frac = (float) (sp.pos - (double) idx);
                const float s0 = 0.5f * (L[idx]     + R[idx]);
                const float s1 = 0.5f * (L[idx + 1] + R[idx + 1]);
                out[i] += (s0 + frac * (s1 - s0)) * sp.gain;   // linear-interp mono sum
                sp.pos += sp.rate;
            }
        }
    }

    void publishTempoToBus()
    {
        const juce::int64 now = juce::Time::currentTimeMillis();
        if (now - lastPublishMs < 250) return;   // throttle to ~4 Hz
        lastPublishMs = now;
        sattari::xplugin::publishTempo ("rhythm-genie",
                                        pubBpm.load (std::memory_order_relaxed),
                                        pubPulse.load (std::memory_order_relaxed));
    }

    float param (const char* id) const noexcept
    {
        if (state == nullptr) return 0.0f;
        return cachedParameterValue (id);
    }

    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::String pfx;
    sattari::host::TransportSnapshot transport;

    // kKitLanes and SampleSlot are declared public above (the shell needs both).
    struct SamplePlayback { SampleSlot::Ptr buf; double pos = 0.0, rate = 1.0; float gain = 1.0f; bool active = false; };

    SampleSlot::Ptr  laneSample[kGridLanes];              // current sample per lane (null = synth)
    SamplePlayback   samplePlay[kGridLanes];              // active playback state per lane
    juce::SpinLock   sampleLock;                          // guards the laneSample swap

    sattari::dsp::DrumKit<kKitLanes> kit;
    sattari::dsp::DrumPadMap         padMap { kKitLanes };
    sattari::dsp::DrumLooper         looper;       // live-pad loop ("looper feel")
    sattari::dsp::BeatGrid           grid;         // editable step grid (8 lanes x 16 steps)
    double loopClockBeats = 0.0;                   // free-running loop position when stopped
    bool   wasLoopOn = false;                      // detect enable edge -> fresh capture
    std::vector<float>               kitScratch;   // mono render buffer (sized in prepare)
    struct KitHit { int lane; float velocity; int offset; };
    static constexpr int kMaxKitHits = 256;
    std::array<KitHit, kMaxKitHits>  kitHits {};
    int kitHitCount = 0;

    // Shaping-param pointers cached in prepare so the audio thread never touches the
    // APVTS map or builds parameter-id strings while rendering.
    std::atomic<float>* laneLevelP[sattari::rhythm::kNumLanes] {};
    std::atomic<float>* laneTuneP [sattari::rhythm::kNumLanes] {};
    std::atomic<float>* laneDecayP[sattari::rhythm::kNumLanes] {};
    std::atomic<float>* kitLevelP = nullptr;

    // Lock-free SPSC FIFO: editor (message thread) -> audio thread live hits.
    struct LiveHit { int kind; int id; float velocity; };   // kind 0 note, 1 key, 2 lane
    static constexpr int kLiveFifoSize = 64;
    std::array<LiveHit, kLiveFifoSize> liveFifo {};
    std::atomic<int> liveWrite { 0 };
    std::atomic<int> liveRead  { 0 };


    double currentSampleRate = 44100.0;
    double internalPpq = 0.0;          // free-run clock when the host has no PPQ
    bool   wasGenerating = false;

    struct PendingOff { int note; int samplesRemaining; };
    static constexpr int kMaxPendingOffs = 256;
    std::array<PendingOff, kMaxPendingOffs> pendingOffs {};
    std::array<PendingOff, kMaxPendingOffs> pendingOffScratch {};
    int pendingOffCount = 0;
    int pendingOffScratchCount = 0;
    juce::Random rng;
    std::atomic<int> playStep { -1 };  // current 16th-step for the editor grid

    // Tempo-bus publishing (audio thread writes atomics; editor timer publishes).
    std::atomic<double>      pubBpm { 120.0 };
    std::atomic<juce::int64> pubPulse { 0 };
    juce::int64 lastPublishMs = 0;
};

} // namespace sattari::dsp
