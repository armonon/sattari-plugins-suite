#include "RhythmGenieProcessor.h"
#include "RhythmGenieEditor.h"

#include <algorithm>

using APVTS = juce::AudioProcessorValueTreeState;
using namespace sattari::rhythm;

namespace
{
constexpr int kDrumChannel = 10;   // GM percussion channel
inline double wrap01 (double p) noexcept { p -= (double) (long long) p; if (p < 0.0) p += 1.0; return p; }
}

juce::StringArray SattariRhythmGenieAudioProcessor::styleChoices()
{
    int n = 0; const auto* b = patterns (n);
    juce::StringArray out;
    for (int i = 0; i < n; ++i) out.add (b[i].name);
    return out;
}

APVTS::ParameterLayout SattariRhythmGenieAudioProcessor::createParameterLayout()
{
    using FloatP  = juce::AudioParameterFloat;
    using ChoiceP = juce::AudioParameterChoice;
    using BoolP   = juce::AudioParameterBool;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] { return juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f); };

    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "style", 1 }, "Style", styleChoices(), 0));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "density",  1 }, "Density",  unit(), 0.5f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "swing",    1 }, "Swing",    unit(), 0.0f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "velocity", 1 }, "Velocity", unit(), 0.8f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "gate",     1 }, "Gate",     unit(), 0.5f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "accent",   1 }, "Accent",   unit(), 0.35f));
    p.push_back (std::make_unique<FloatP>  (juce::ParameterID { "humanize", 1 }, "Humanize", unit(), 0.2f));

    static const char* laneIds[kNumLanes] = { "kickOn", "snareOn", "chatOn", "ohatOn" };
    for (int l = 0; l < kNumLanes; ++l)
        p.push_back (std::make_unique<BoolP> (juce::ParameterID { laneIds[l], 1 },
                                              juce::String (laneName (l)) + " On", true));

    // Audio kit: master output + per-lane sound shaping (level / tune / decay) for the
    // four sequencer lanes. Rhythm Genie is now a drum instrument, not just a MIDI source.
    p.push_back (std::make_unique<FloatP> (juce::ParameterID { "kitLevel", 1 }, "Kit Level", unit(), 0.8f));
    static const char* base[kNumLanes] = { "kick", "snare", "chat", "ohat" };
    for (int l = 0; l < kNumLanes; ++l)
    {
        const juce::String nm (laneName (l));
        p.push_back (std::make_unique<FloatP> (juce::ParameterID { base[l] + juce::String ("Level"), 1 }, nm + " Level", unit(), 0.8f));
        p.push_back (std::make_unique<FloatP> (juce::ParameterID { base[l] + juce::String ("Tune"),  1 }, nm + " Tune",  unit(), 0.5f));
        p.push_back (std::make_unique<FloatP> (juce::ParameterID { base[l] + juce::String ("Decay"), 1 }, nm + " Decay", unit(), 0.5f));
    }

    // Seamless loop: capture live pad hits and repeat them every N bars ("looper feel").
    p.push_back (std::make_unique<BoolP>   (juce::ParameterID { "loopOn", 1 }, "Loop", false));
    p.push_back (std::make_unique<ChoiceP> (juce::ParameterID { "loopBars", 1 }, "Loop Bars",
                                            juce::StringArray { "1", "2", "4" }, 1));

    return { p.begin(), p.end() };
}

SattariRhythmGenieAudioProcessor::SattariRhythmGenieAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    grid.setLanes (kGridLanes);
    grid.setSteps (kSteps);
    registerFactoryPresets();
    regenerateGrid();   // seed an editable groove from the default style
    formatManager.registerBasicFormats();
}

// Genre-flavoured factory presets. Each sets the style + feel; loading one (setCurrentProgram)
// regenerates the grid so the pattern actually changes. Styles: 0 Four-on-Floor, 1 Boom Bap,
// 2 Trap, 3 Pop, 4 Breakbeat, 5 Half-Time.
void SattariRhythmGenieAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    presetBank.addFactoryPreset ("House",       PV{ {"style",0}, {"density",0.70f}, {"swing",0.08f}, {"velocity",0.85f}, {"accent",0.40f}, {"humanize",0.15f} });
    presetBank.addFactoryPreset ("Techno Drive",PV{ {"style",0}, {"density",0.88f}, {"swing",0.00f}, {"velocity",0.92f}, {"accent",0.35f}, {"humanize",0.10f}, {"chatDecay",0.60f} });
    presetBank.addFactoryPreset ("Boom Bap",    PV{ {"style",1}, {"density",0.50f}, {"swing",0.18f}, {"velocity",0.80f}, {"accent",0.45f}, {"humanize",0.35f} });
    presetBank.addFactoryPreset ("Lo-Fi Swing", PV{ {"style",1}, {"density",0.45f}, {"swing",0.26f}, {"velocity",0.72f}, {"accent",0.30f}, {"humanize",0.50f}, {"kitLevel",0.72f} });
    presetBank.addFactoryPreset ("Trap",        PV{ {"style",2}, {"density",0.75f}, {"swing",0.00f}, {"velocity",0.85f}, {"accent",0.55f}, {"humanize",0.12f}, {"kickDecay",0.70f} });
    presetBank.addFactoryPreset ("Pop",         PV{ {"style",3}, {"density",0.55f}, {"swing",0.10f}, {"velocity",0.82f}, {"accent",0.40f}, {"humanize",0.20f} });
    presetBank.addFactoryPreset ("Breakbeat",   PV{ {"style",4}, {"density",0.72f}, {"swing",0.05f}, {"velocity",0.85f}, {"accent",0.45f}, {"humanize",0.30f} });
    presetBank.addFactoryPreset ("Half-Time",   PV{ {"style",5}, {"density",0.50f}, {"swing",0.00f}, {"velocity",0.80f}, {"accent",0.50f}, {"humanize",0.18f} });
}

int SattariRhythmGenieAudioProcessor::kitLaneNote (int lane) noexcept
{
    // GM drum map for the 8 kit voices (kick snare chat ohat clap tom rim ride).
    static const int notes[kGridLanes] = { 36, 38, 42, 46, 39, 45, 37, 51 };
    return (lane >= 0 && lane < kGridLanes) ? notes[lane] : 36;
}

// Fill the drum lanes (0-3) from the current style + density; hand-placed sounds on
// lanes 4-7 survive (SattariGroove only touches 0-3). Called from the constructor and
// the editor's Generate button.
void SattariRhythmGenieAudioProcessor::regenerateGrid()
{
    using GS = sattari::dsp::GrooveStyle;
    // Style order: Four on Floor / Boom Bap / Trap / Pop / Breakbeat / Half-Time.
    static const GS map[6] = { GS::FourFloor, GS::BoomBap, GS::Trap, GS::House, GS::Breakbeat, GS::HalfTime };
    const int styleIdx = juce::jlimit (0, 5, (int) std::round (param ("style")));
    grid.setLanes (kGridLanes);
    grid.setSteps (kSteps);
    sattari::dsp::generate (grid, map[styleIdx], param ("density"), (std::uint32_t) (rng.nextInt() | 1));
}

// --- Sample slots ----------------------------------------------------------------
void SattariRhythmGenieAudioProcessor::setLaneSampleBuffer (int lane, juce::AudioBuffer<float> buf,
                                                            double sourceSr, const juce::String& name)
{
    if (lane < 0 || lane >= kGridLanes) return;
    SampleSlot::Ptr s = new SampleSlot();
    s->data = std::move (buf);
    s->sourceSr = sourceSr > 0.0 ? sourceSr : 44100.0;
    s->name = name;
    retainedSamples.add (s);                              // keep alive for the session (msg thread)
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    laneSample[lane] = s;                                 // fast pointer swap
}

bool SattariRhythmGenieAudioProcessor::loadSampleForLane (int lane, const juce::File& file)
{
    if (lane < 0 || lane >= kGridLanes || ! file.existsAsFile()) return false;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr) return false;
    const int len = (int) juce::jmin ((juce::int64) (reader->sampleRate * 12.0), reader->lengthInSamples);  // cap 12 s
    if (len <= 0) return false;
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);
    setLaneSampleBuffer (lane, std::move (buf), reader->sampleRate, file.getFileName());
    laneSamplePath[lane] = file.getFullPathName();
    return true;
}

void SattariRhythmGenieAudioProcessor::clearSampleForLane (int lane) noexcept
{
    if (lane < 0 || lane >= kGridLanes) return;
    laneSamplePath[lane].clear();
    const juce::SpinLock::ScopedLockType sl (sampleLock);
    laneSample[lane] = nullptr;                           // falls back to the synth voice
}

bool SattariRhythmGenieAudioProcessor::laneHasSample (int lane) const noexcept
{
    return lane >= 0 && lane < kGridLanes && laneSample[lane] != nullptr;
}

juce::String SattariRhythmGenieAudioProcessor::laneSampleName (int lane) const
{
    if (lane >= 0 && lane < kGridLanes && laneSample[lane] != nullptr) return laneSample[lane]->name;
    return {};
}

void SattariRhythmGenieAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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
    static const char* base[kNumLanes] = { "kick", "snare", "chat", "ohat" };
    for (int l = 0; l < kNumLanes; ++l)
    {
        laneLevelP[l] = apvts.getRawParameterValue (base[l] + juce::String ("Level"));
        laneTuneP[l]  = apvts.getRawParameterValue (base[l] + juce::String ("Tune"));
        laneDecayP[l] = apvts.getRawParameterValue (base[l] + juce::String ("Decay"));
    }
    kitLevelP = apvts.getRawParameterValue ("kitLevel");
}

bool SattariRhythmGenieAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

bool SattariRhythmGenieAudioProcessor::laneEnabled (int lane) const noexcept
{
    static const char* ids[kNumLanes] = { "kickOn", "snareOn", "chatOn", "ohatOn" };
    if (lane < 0 || lane >= kGridLanes) return false;
    if (lane < kNumLanes) return apvts.getRawParameterValue (ids[lane])->load() > 0.5f;
    return true;   // lanes 4-7 (clap/tom/rim/ride) have no mute toggle; always active
}

void SattariRhythmGenieAudioProcessor::allNotesOff (juce::MidiBuffer& out)
{
    for (int i = 0; i < pendingOffCount; ++i)
    {
        const auto& po = pendingOffs[(size_t) i];
        out.addEvent (juce::MidiMessage::noteOff (kDrumChannel, po.note), 0);
    }
    pendingOffCount = 0;
    pendingOffScratchCount = 0;
}

void SattariRhythmGenieAudioProcessor::emitStep (int stepIdx, int sampleOffset,
                                                 juce::MidiBuffer& out, double samplesPerStep)
{
    const float vBase = param ("velocity");
    const float acc   = param ("accent");
    const float hum   = param ("humanize");
    const int   gateS = juce::jmax (1, (int) (param ("gate") * samplesPerStep));

    for (int lane = 0; lane < kGridLanes; ++lane)
    {
        // The editable grid is the source of truth: a cell fires (probability-gated) and
        // carries its own velocity, so hand-drawn / generated patterns both work.
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

void SattariRhythmGenieAudioProcessor::queueKitHit (int lane, float velocity, int sampleOffset) noexcept
{
    if (kitHitCount < kMaxKitHits)
        kitHits[(size_t) kitHitCount++] = { lane, velocity, sampleOffset };
}

// --- Editor -> audio live triggering ---------------------------------------------
void SattariRhythmGenieAudioProcessor::pushLive (int kind, int id, float velocity) noexcept
{
    const int w = liveWrite.load (std::memory_order_relaxed);
    const int nextW = (w + 1) % kLiveFifoSize;
    if (nextW == liveRead.load (std::memory_order_acquire)) return;   // full: drop the gesture
    liveFifo[(size_t) w] = { kind, id, juce::jlimit (0.0f, 1.0f, velocity) };
    liveWrite.store (nextW, std::memory_order_release);
}

void SattariRhythmGenieAudioProcessor::pushLiveNote (int note, float velocity) noexcept { pushLive (0, note, velocity); }
void SattariRhythmGenieAudioProcessor::pushLiveKey  (int keyCode, float velocity) noexcept { pushLive (1, keyCode, velocity); }
void SattariRhythmGenieAudioProcessor::pushLiveLane (int lane, float velocity) noexcept { pushLive (2, lane, velocity); }

// Drain on the audio thread: pad-map binding (learn) resolves here, so a "Set" armed from
// the editor captures whichever note/key the user next plays. When looping, live hits are
// also recorded into the loop at `phase` so overdubs join the cycle.
void SattariRhythmGenieAudioProcessor::drainLiveHits (bool recordToLoop, double phase) noexcept
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

sattari::dsp::DrumVoiceParams SattariRhythmGenieAudioProcessor::laneVoiceParams (int lane) const noexcept
{
    sattari::dsp::DrumVoiceParams vp;
    // Only the four sequencer lanes have shaping params; MIDI-only lanes 4..7 use defaults.
    // Pointers are resolved in prepare, so this is lock-free and allocation-free.
    if (lane >= 0 && lane < kNumLanes && laneLevelP[lane] != nullptr)
    {
        vp.level = laneLevelP[lane]->load (std::memory_order_relaxed);
        vp.tune  = laneTuneP[lane]->load (std::memory_order_relaxed);
        vp.decay = laneDecayP[lane]->load (std::memory_order_relaxed);
    }
    return vp;
}

// Segmented, sample-accurate render: advance the kit to each hit's offset, trigger, then
// finish the block. Runs every block (even stopped) so tails ring out and live pads sound.
void SattariRhythmGenieAudioProcessor::renderKit (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
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
void SattariRhythmGenieAudioProcessor::triggerLane (int lane, float velocity) noexcept
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
void SattariRhythmGenieAudioProcessor::renderKitSegment (float* out, int numSamples) noexcept
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

void SattariRhythmGenieAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples = buffer.getNumSamples();

    // --- Transport + loop phase (computed first so live hits can be time-stamped) ------
    const auto t = sattari::host::read (*this);
    const double bpm = t.bpm > 0.0 ? t.bpm : 120.0;
    pubBpm.store (bpm, std::memory_order_relaxed);   // shared to the cross-plugin tempo bus
    const double samplesPerBeat = currentSampleRate * 60.0 / bpm;
    const double samplesPerStep = samplesPerBeat / 4.0;
    const double blockBeats     = (double) numSamples / samplesPerBeat;
    const bool standalone = (wrapperType == wrapperType_Standalone);

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
        emitStep (((k % kSteps) + kSteps) % kSteps, offset, generated, samplesPerStep);
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
    playStep.store ((((int) std::floor (startPpq * 4.0)) % kSteps + kSteps) % kSteps);
}

void SattariRhythmGenieAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
    {
        // Persist the pad bindings (learned MIDI notes / keys) alongside the params.
        auto* pads = xml->createNewChildElement ("PADMAP");
        const auto v = padMap.toState();
        pads->setAttribute ("n", (int) v.size());
        for (int i = 0; i < (int) v.size(); ++i)
            pads->setAttribute ("b" + juce::String (i), v[(size_t) i]);

        // Persist the editable grid (only the ON cells, so it stays compact).
        auto* g = xml->createNewChildElement ("GRID");
        g->setAttribute ("lanes", grid.lanes());
        g->setAttribute ("steps", grid.steps());
        for (int l = 0; l < grid.lanes(); ++l)
            for (int s = 0; s < grid.steps(); ++s)
                if (grid.on (l, s))
                {
                    auto* c = g->createNewChildElement ("c");
                    c->setAttribute ("l", l);
                    c->setAttribute ("s", s);
                    c->setAttribute ("v", grid.velocity (l, s));
                    c->setAttribute ("p", grid.probability (l, s));
                }

        // Persist per-lane sample file paths (reloaded on restore).
        auto* smp = xml->createNewChildElement ("SAMPLES");
        for (int l = 0; l < kGridLanes; ++l)
            if (laneSamplePath[l].isNotEmpty())
            {
                auto* e = smp->createNewChildElement ("s");
                e->setAttribute ("l", l);
                e->setAttribute ("path", laneSamplePath[l]);
            }
        copyXmlToBinary (*xml, destData);
    }
}

void SattariRhythmGenieAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (auto* pads = xml->getChildByName ("PADMAP"))
        {
            const int n = juce::jlimit (0, 512, pads->getIntAttribute ("n", 0));
            std::vector<int> v ((size_t) n, -1);
            for (int i = 0; i < n; ++i)
                v[(size_t) i] = pads->getIntAttribute ("b" + juce::String (i), -1);
            padMap.fromState (v);
        }
        if (auto* g = xml->getChildByName ("GRID"))
        {
            grid.clear();
            grid.setLanes (g->getIntAttribute ("lanes", kGridLanes));
            grid.setSteps (g->getIntAttribute ("steps", kSteps));
            for (auto* c : g->getChildWithTagNameIterator ("c"))
                grid.set (c->getIntAttribute ("l", -1), c->getIntAttribute ("s", -1), true,
                          (float) c->getDoubleAttribute ("v", 0.8),
                          (float) c->getDoubleAttribute ("p", 1.0));
        }
        if (auto* smp = xml->getChildByName ("SAMPLES"))
            for (auto* e : smp->getChildWithTagNameIterator ("s"))
            {
                const int l = e->getIntAttribute ("l", -1);
                const juce::File f (e->getStringAttribute ("path"));
                if (l >= 0 && l < kGridLanes && f.existsAsFile()) loadSampleForLane (l, f);
            }
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

void SattariRhythmGenieAudioProcessor::publishTempoToBus()
{
    const juce::int64 now = juce::Time::currentTimeMillis();
    if (now - lastPublishMs < 250) return;   // throttle to ~4 Hz
    lastPublishMs = now;
    sattari::xplugin::publishTempo ("rhythm-genie",
                                    pubBpm.load (std::memory_order_relaxed),
                                    pubPulse.load (std::memory_order_relaxed));
}

juce::AudioProcessorEditor* SattariRhythmGenieAudioProcessor::createEditor()
{
    return new SattariRhythmGenieAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariRhythmGenieAudioProcessor();
}
