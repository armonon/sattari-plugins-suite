#include "MainPluginProcessor.h"
#include "MainPluginEditor.h"

namespace
{
class AudioLoadJob final : public juce::ThreadPoolJob
{
public:
    AudioLoadJob (juce::File f,
                  std::function<std::shared_ptr<const DeckPlayer::LoadedAudio>(const juce::File&)> loader,
                  std::function<void(std::shared_ptr<const DeckPlayer::LoadedAudio>)> done)
        : ThreadPoolJob ("StemDeck audio load"), file (std::move (f)), load (std::move (loader)), onDone (std::move (done)) {}

    JobStatus runJob() override
    {
        auto loaded = load (file);
        juce::MessageManager::callAsync ([loaded, cb = onDone]() mutable
        {
            if (cb)
                cb (std::move (loaded));
        });
        return jobHasFinished;
    }

private:
    juce::File file;
    std::function<std::shared_ptr<const DeckPlayer::LoadedAudio>(const juce::File&)> load;
    std::function<void(std::shared_ptr<const DeckPlayer::LoadedAudio>)> onDone;
};

// This build decodes bare char* literals as Latin-1; wrap non-ASCII text so it
// is interpreted as UTF-8 (otherwise "•" / "→" render as mojibake).
inline juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id, const juce::String& name,
                                                       float min, float max, float def, const juce::String& suffix = {})
{
    juce::NormalisableRange<float> range (min, max, 0.001f);   // keep fine internal precision
    const auto unit = suffix.trim();                            // host-facing label only
    // Display 1 decimal place so readouts stay short ("120.0") instead of the
    // 0.001-interval default "120.000". The Slider appends its own unit suffix, so the
    // value text must NOT include the unit here, or it would double ("0.0 dB dB").
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
        juce::AudioParameterFloatAttributes()
            .withLabel (unit)
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); }));
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam (const juce::String& id, const juce::String& name,
                                                         const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (id, 1), name, choices, def);
}

juce::File withSuffix (const juce::File& file, const juce::String& suffix)
{
    return file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension() + suffix).withFileExtension ("wav");
}

juce::String deckNameFor (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, MainPluginProcessor::numDecks - 1, deckIndex)));
}

// Crossfader response law. Returns { A/C-bus gain, B/D-bus gain } for a fader
// position in [0,1], shaped by `curve` (0 Smooth, 1 Linear, 2 Sharp, 3 Cut).
// `reverse` (hamster) flips the fader sense so the two busses trade ends.
inline std::pair<float, float> crossfaderBusGains (float x, int curve, bool reverse) noexcept
{
    x = juce::jlimit (0.0f, 1.0f, x);
    if (reverse)
        x = 1.0f - x;

    const float halfPi = juce::MathConstants<float>::halfPi;
    switch (juce::jlimit (0, 3, curve))
    {
        case 1:   // Linear — constant-sum; gentle -6 dB dip through the centre.
            return { 1.0f - x, x };

        case 2:   // Sharp — equal-power on a centre-steepened position warp.
        {
            const float d  = x - 0.5f;                                   // -0.5..0.5
            const float w  = std::copysign (std::pow (std::abs (d) * 2.0f, 0.45f), d);
            const float xs = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * w); // steeper near centre
            return { std::cos (xs * halfPi), std::sin (xs * halfPi) };
        }

        case 3:   // Cut / transform — each side full except in a narrow edge band.
        {
            const float band = 0.10f;
            return { juce::jlimit (0.0f, 1.0f, (1.0f - x) / band),
                     juce::jlimit (0.0f, 1.0f, x / band) };
        }

        default:  // Smooth — classic equal-power blend.
            return { std::cos (x * halfPi), std::sin (x * halfPi) };
    }
}
}

MainPluginProcessor::MainPluginProcessor()
    : AudioProcessor (BusesProperties().withInput ("Mic / Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    formatManager.registerBasicFormats();
#if JUCE_USE_MP3AUDIOFORMAT
    formatManager.registerFormat (new juce::MP3AudioFormat(), true);
#endif
    apvts.state.setProperty ("schema", "SattariStemDeck.v0.3.8", nullptr);
    for (int i = 0; i < numDecks; ++i)
    {
        liveKeyPitchClass[(size_t) i].store (-1, std::memory_order_relaxed);
        liveKeyIsMinor[(size_t) i].store (0, std::memory_order_relaxed);
        liveKeyConfidence[(size_t) i].store (0.0f, std::memory_order_relaxed);
    }
    cacheDeckParamIds();
    buildMidiTargets();
    ensureTeWrapperOnMessageThread();
}

void MainPluginProcessor::cacheDeckParamIds()
{
    for (int i = 0; i < numDecks; ++i)
    {
        const auto d = deckPrefix (i);
        auto& p = deckIds[(size_t) i];
        p.bpm = d + "Bpm";  p.beatOffset = d + "BeatOffset";  p.downbeat = d + "Downbeat";
        p.tempoInterp = d + "TempoInterpretation";  p.syncMode = d + "SyncMode";
        p.keyTarget = d + "KeyTarget";  p.pitch = d + "Pitch";
        p.play = d + "Play";  p.liveKey = d + "LiveKey";  p.keyLock = d + "KeyLock";
        p.volume = d + "Volume";  p.fader = d + "Fader";
        p.low = d + "Low";  p.mid = d + "Mid";  p.high = d + "High";  p.filter = d + "Filter";
        p.revMix = d + "RevMix";  p.echoMix = d + "EchoMix";
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto s = d + "Stem" + DeckPlayer::stemLaneId (lane);
            p.stemGain [(size_t) lane] = s + "Gain";
            p.stemPitch[(size_t) lane] = s + "Pitch";
            p.stemFilt [(size_t) lane] = s + "Filt";
            p.stemSend [(size_t) lane] = s + "Send";
        }
    }
}

void MainPluginProcessor::buildMidiTargets()
{
    std::vector<MidiMappingManager::CcTarget> cc;
    auto addCc = [&] (const juce::String& name, const juce::String& paramId)
    {
        cc.push_back ({ name, apvts.getParameter (paramId) });
    };

    addCc ("Crossfader",    "crossfader");
    addCc ("Master BPM",    "masterBpm");
    addCc ("Master Volume", "master");
    for (int i = 0; i < numDecks; ++i)
    {
        const auto label = deckLabel (i);
        addCc (label + " Volume", deckPrefix (i) + "Volume");
        addCc (label + " Filter", deckPrefix (i) + "Filter");
        addCc (label + " Low",    deckPrefix (i) + "Low");
        addCc (label + " High",   deckPrefix (i) + "High");
        // Per-stem faders (drums/bass/music/vocals) so an MPK-style controller can
        // ride individual stems.
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
            addCc (label + " " + DeckPlayer::stemLaneLabel (lane) + " Stem",
                   deckPrefix (i) + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain");
    }

    std::vector<MidiMappingManager::NoteTarget> notes;
    for (int i = 0; i < numDecks; ++i)
    {
        notes.push_back ({ deckLabel (i) + " Play", i, MidiMappingManager::ActionPlay });
        notes.push_back ({ deckLabel (i) + " Cue",  i, MidiMappingManager::ActionCue  });
    }
    for (int pad = 0; pad < PadBank::numPads; ++pad)   // pad triggers -> hardware pads
        notes.push_back ({ "Pad " + juce::String (pad + 1), pad, MidiMappingManager::ActionPad });

    midiMapping.setTargets (std::move (cc), std::move (notes));
}

MainPluginProcessor::~MainPluginProcessor()
{
    loadPool.removeAllJobs (true, 1500);
}

void MainPluginProcessor::ensureTeWrapperOnMessageThread()
{
    if (teWrapper != nullptr)
        return;

    auto create = [this]
    {
        if (teWrapper == nullptr)
            teWrapper = std::make_unique<TeWrapper>();
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        create();
    }
    else
    {
        jassert (! juce::MessageManager::getInstance()->currentThreadHasLockedMessageManager());
        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([&] { create(); done.signal(); });
        done.wait (-1);
    }
}

juce::String MainPluginProcessor::deckPrefix (int deckIndex)
{
    return "deck" + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, numDecks - 1, deckIndex)));
}

juce::String MainPluginProcessor::deckLabel (int deckIndex)
{
    return "Deck " + juce::String::charToString ((juce_wchar) ('A' + juce::jlimit (0, numDecks - 1, deckIndex)));
}

juce::StringArray MainPluginProcessor::keyTargetChoices()
{
    juce::StringArray choices { "Off" };
    for (int pc = 0; pc < 12; ++pc)
        choices.add (TrackAnalyzer::pitchClassName (pc));
    return choices;
}

int MainPluginProcessor::keyTargetChoiceToPitchClass (int choiceIndex)
{
    return choiceIndex > 0 ? juce::jlimit (0, 11, choiceIndex - 1) : -1;
}

juce::AudioProcessorValueTreeState::ParameterLayout MainPluginProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < numDecks; ++i)
    {
        const auto deck = deckPrefix (i);
        const auto label = deckLabel (i);
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (deck + "Play", 1), label + " Play", false));
        params.push_back (floatParam (deck + "Bpm", label + " Source BPM", 40.0f, 240.0f, 120.0f, " BPM"));
        params.push_back (choiceParam (deck + "TempoInterpretation", label + " Tempo Interpretation", BeatSyncEngine::tempoInterpretationChoices(), 0));
        params.push_back (choiceParam (deck + "SyncMode", label + " Sync Mode", BeatSyncEngine::syncModeChoices(), 0));
        params.push_back (floatParam (deck + "Volume", label + " Gain", -60.0f, 6.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "Fader", label + " Channel Fader", 0.0f, 1.0f, 1.0f));
        params.push_back (floatParam (deck + "Pitch", label + " Pitch Shift", -12.0f, 12.0f, 0.0f, " st"));
        params.push_back (floatParam (deck + "BeatOffset", label + " Beat Grid Offset", -4.0f, 4.0f, 0.0f, " beats"));
        params.push_back (floatParam (deck + "Downbeat", label + " Downbeat Position", -16.0f, 16.0f, 0.0f, " beats"));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (deck + "KeyLock", 1), label + " Pitch Preserve Request", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (deck + "LiveKey", 1), label + " Live Key Analysis", false));
        params.push_back (choiceParam (deck + "KeyTarget", label + " Match Target Key", keyTargetChoices(), 0));
        params.push_back (floatParam (deck + "Low", label + " Low", -24.0f, 12.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "Mid", label + " Mid", -24.0f, 12.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "High", label + " High", -24.0f, 12.0f, 0.0f, "dB"));
        params.push_back (floatParam (deck + "Filter", label + " Filter", -1.0f, 1.0f, 0.0f));

        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
            params.push_back (floatParam (deck + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain",
                                          label + " " + DeckPlayer::stemLaneLabel (lane) + " Stem Gain",
                                          -60.0f, 12.0f, lane == DeckPlayer::fullMixStem ? 0.0f : 0.0f, "dB"));

        // Independent per-stem pitch (semitones) — pitch e.g. the vocal acapella while
        // the instrumental stays in key. Only the 4 separated stems, not the full mix.
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
            params.push_back (floatParam (deck + "Stem" + DeckPlayer::stemLaneId (lane) + "Pitch",
                                          label + " " + DeckPlayer::stemLaneLabel (lane) + " Stem Pitch",
                                          -12.0f, 12.0f, 0.0f, " st"));

        // Per-stem FX (real-time separation path): independent filter + space/echo
        // send for each of the four stems. Filt 0.5 = neutral, Send 0 = dry.
        for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
        {
            const auto id = DeckPlayer::stemLaneId (lane);
            const auto lbl = DeckPlayer::stemLaneLabel (lane);
            params.push_back (floatParam (deck + "Stem" + id + "Filt", label + " " + lbl + " Stem Filter", 0.0f, 1.0f, 0.5f));
            params.push_back (floatParam (deck + "Stem" + id + "Send", label + " " + lbl + " Stem Space/Echo", 0.0f, 1.0f, 0.0f));
        }

        params.push_back (floatParam (deck + "RevMix",  label + " Reverb Mix",   0.0f, 1.0f, 0.0f));
        params.push_back (floatParam (deck + "EchoMix", label + " Echo Mix",     0.0f, 1.0f, 0.0f));
    }

    params.push_back (floatParam ("crossfader", "Crossfader", 0.0f, 1.0f, 0.5f));
    // Crossfader response: 0 Smooth (equal-power, long blends) · 1 Linear (constant-sum) ·
    // 2 Sharp (fast crossover) · 3 Cut (scratch/transform — near-instant near either edge).
    params.push_back (choiceParam ("crossfaderCurve", "Crossfader Curve",
                                   { "Smooth", "Linear", "Sharp", "Cut" }, 0));
    // Hamster / reverse: swaps the fader sense so A/C and B/D trade ends.
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("crossfaderReverse", 1), "Crossfader Reverse", false));
    params.push_back (floatParam ("masterBpm", "Global Master BPM", 40.0f, 240.0f, 120.0f, " BPM"));
    params.push_back (floatParam ("master", "Master", -60.0f, 6.0f, 0.0f, "dB"));
    params.push_back (floatParam ("limiterCeiling", "Limiter Ceiling", -12.0f, -0.1f, -1.0f, "dB"));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("hostSync", 1), "Host Tempo Sync", true));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("beatSync", 1), "Host Beat/Grid Sync", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("micMonitor", 1), "Mic/Input Monitor", false));
    params.push_back (floatParam ("micGain", "Mic/Input Gain", -60.0f, 6.0f, -12.0f, "dB"));
    // Global Master Effects XY pad: X = bipolar filter, Y = space/echo send.
    params.push_back (floatParam ("masterFxX", "Master FX Filter", 0.0f, 1.0f, 0.5f));
    params.push_back (floatParam ("masterFxY", "Master FX Space/Echo", 0.0f, 1.0f, 0.0f));
    // Smart mastering assistant: auto EQ tilt + bus dynamics + LUFS targeting.
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("smartMasterOn", 1), "AI Master", false));
    params.push_back (choiceParam ("smartMasterMode", "AI Master Mode",
                                   { "Streaming -14", "Club -9", "Warm -16" }, 0));
    params.push_back (floatParam ("smartMasterAmount", "AI Master Amount", 0.0f, 1.0f, 0.8f));
    params.push_back (choiceParam ("masterKey", "Master Key Target", keyTargetChoices(), 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID ("stemLink", 1), "Link Stem Gains Across Decks", false));

    for (int i = 0; i < PadBank::numPads; ++i)
        params.push_back (floatParam ("pad" + juce::String (i + 1) + "Gain", "Pad " + juce::String (i + 1) + " Gain", -60.0f, 6.0f, 0.0f, "dB"));

    return { params.begin(), params.end() };
}

void MainPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    limiterEnvelope = 1.0f;
    const int outputChannels = juce::jmax (1, getTotalNumOutputChannels());
    for (auto& deck : decks)
        deck.prepare (sampleRate, samplesPerBlock, outputChannels);
    padBank.prepare (sampleRate);
    masterRecorder.prepare (sampleRate, outputChannels, samplesPerBlock);
    micRecorder.prepare (sampleRate, getTotalNumInputChannels() > 0 ? getTotalNumInputChannels() : outputChannels, samplesPerBlock);
    for (auto& recorder : deckRecorders)
        recorder.prepare (sampleRate, outputChannels, samplesPerBlock);
    for (auto& scratch : deckScratchBuffers)
        scratch.setSize (outputChannels, samplesPerBlock);
    micScratch.setSize (juce::jmax (outputChannels, getTotalNumInputChannels()), samplesPerBlock);
    teScratch.setSize (juce::jmax (2, outputChannels), samplesPerBlock);
    teMidiScratch.ensureSize (256);
    warpEngine.prepare (sampleRate, samplesPerBlock, outputChannels);
    hostGraph.prepare (sampleRate, samplesPerBlock, outputChannels);
    masterFx.prepare (sampleRate, samplesPerBlock, outputChannels);
    smartMaster.prepare (sampleRate);
    for (auto& deckStemFx : stemFx)
        for (auto& fx : deckStemFx)
            fx.prepare (sampleRate, samplesPerBlock, outputChannels);
    for (auto& fx : deckFX)
        fx.prepare (sampleRate, samplesPerBlock);
    arrangementPlayer.prepare (sampleRate, samplesPerBlock);
    midiClipPlayer.prepare (sampleRate, samplesPerBlock);
    metronome.prepare (sampleRate);
    ensureTeWrapperOnMessageThread();
    if (teWrapper != nullptr)
    {
        tracktion::HostedAudioDeviceInterface::Parameters teParams;
        teParams.sampleRate    = sampleRate;
        teParams.blockSize     = samplesPerBlock;
        teParams.inputChannels  = getTotalNumInputChannels();
        teParams.outputChannels = juce::jmax (2, getTotalNumOutputChannels());
        teWrapper->audioInterface.initialise (teParams);
        teWrapper->audioInterface.prepareToPlay (sampleRate, samplesPerBlock);
    }
    for (auto& rtss : deckRTSS)
        rtss.prepare (sampleRate, samplesPerBlock);
    for (int i = 0; i < numDecks; ++i)
    {
        rtssDrumsBuffers [(size_t) i].setSize (outputChannels, samplesPerBlock, false, true, true);
        rtssBassBuffers  [(size_t) i].setSize (outputChannels, samplesPerBlock, false, true, true);
        rtssMusicBuffers [(size_t) i].setSize (outputChannels, samplesPerBlock, false, true, true);
        rtssVocalsBuffers[(size_t) i].setSize (outputChannels, samplesPerBlock, false, true, true);
    }
}

bool MainPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::disabled()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* MainPluginProcessor::createEditor()
{
    return new MainPluginEditor (*this);
}

float MainPluginProcessor::getFloatParam (juce::StringRef id) const
{
    if (auto* value = apvts.getRawParameterValue (id))
        return value->load();
    return 0.0f;
}

bool MainPluginProcessor::getBoolParam (juce::StringRef id) const
{
    if (auto* value = apvts.getRawParameterValue (id))
        return value->load() > 0.5f;
    return false;
}

void MainPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // ── Hardware MIDI controller mapping ──────────────────────────────────────
    // CC messages are applied to mapped parameters inside processBlock();
    // note triggers are returned for us to dispatch as deck actions.
    {
        const int nTriggers = midiMapping.processBlock (midi);
        for (int t = 0; t < nTriggers; ++t)
        {
            const auto tr = midiMapping.triggered (t);

            if (tr.action == MidiMappingManager::ActionPad)   // tr.deck holds the pad index
            {
                if (juce::isPositiveAndBelow (tr.deck, PadBank::numPads))
                    padBank.triggerPad (tr.deck);             // RT-safe primitive (no logging)
                continue;
            }

            if (! juce::isPositiveAndBelow (tr.deck, numDecks)) continue;

            if (tr.action == MidiMappingManager::ActionPlay)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (deckPrefix (tr.deck) + "Play")))
                    p->setValueNotifyingHost (p->get() ? 0.0f : 1.0f);
            }
            else if (tr.action == MidiMappingManager::ActionCue)
            {
                decks[(size_t) tr.deck].seekToSamples (0.0);   // RT-safe queued seek
            }
        }
    }

    // Mic capture reuses the pre-allocated micScratch (no RT alloc). micCaptured
    // marks whether it holds this block's input (replaces the old getNumSamples check).
    juce::AudioBuffer<float>& micInput = micScratch;
    bool micCaptured = false;
    const bool isCapturingPerformance = masterRecorder.isRecording();
    if ((getBoolParam ("micMonitor") || isCapturingPerformance) && getTotalNumInputChannels() > 0)
    {
        micInput.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            micInput.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());
        micCaptured = true;
    }

    buffer.clear();

    auto syncSnapshot = sync.update (getPlayHead());
    const float masterBpm = juce::jlimit (40.0f, 240.0f, getFloatParam ("masterBpm"));
    bool anyDeckPlaying = false;
    for (int i = 0; i < numDecks; ++i)
        anyDeckPlaying = anyDeckPlaying || getBoolParam (deckPrefix (i) + "Play");
    if (! syncSnapshot.hostPlaying)
    {
        syncSnapshot.bpm = masterBpm;
        syncSnapshot.hostPlaying = anyDeckPlaying;
        syncSnapshot.ppqPosition = currentSampleRate > 0.0 ? ((double) transportSampleCounter / currentSampleRate) * (double) masterBpm / 60.0 : 0.0;
    }
    const int64_t blockRecordingStart = recordingSampleCounter.load (std::memory_order_relaxed);
    updatePerformanceCaptureState (blockRecordingStart, syncSnapshot);

    const float crossfader = getFloatParam ("crossfader");
    const int   xfCurve    = (int) std::round (getFloatParam ("crossfaderCurve"));
    const bool  xfReverse  = getBoolParam ("crossfaderReverse");
    const auto  xfGains    = crossfaderBusGains (crossfader, xfCurve, xfReverse);
    const float leftBusGain  = xfGains.first;    // A/C bus
    const float rightBusGain = xfGains.second;   // B/D bus

    for (int i = 0; i < numDecks; ++i)
    {
        const auto& ids = deckIds[(size_t) i];   // pre-concatenated param ids (no RT alloc)
        const int cfSide = getDeckCfSide (i);   // 0=A(left), 1=Center, 2=B(right)
        const float deckMixGain = (cfSide == 0) ? leftBusGain
                                : (cfSide == 2) ? rightBusGain
                                : 1.0f;   // Center: unaffected by crossfader
        const auto deckAudio = decks[(size_t) i].getLoadedAudio();

        // Rebuild the beat grid only when its inputs change — makeGrid copies two
        // vectors and sorts, which must not run every block on the audio thread.
        const float deckBpm      = getFloatParam (ids.bpm);
        const float deckBeatOff  = getFloatParam (ids.beatOffset);
        const float deckDownbeat = getFloatParam (ids.downbeat);
        const int   deckInterp   = (int) std::round (getFloatParam (ids.tempoInterp));
        const int   deckSyncMode = (int) std::round (getFloatParam (ids.syncMode));
        const GridCacheKey gridKey { deckAudio.get(), deckBpm, deckBeatOff, deckDownbeat, deckInterp, deckSyncMode };
        if (gridKey != cachedGridKey[(size_t) i])
        {
            cachedBeatGrid[(size_t) i] = BeatSyncEngine::makeGrid (getDeckAnalysis (i), deckBpm, deckBeatOff, deckDownbeat,
                                                                   BeatSyncEngine::tempoInterpretationFromChoice (deckInterp),
                                                                   BeatSyncEngine::syncModeFromChoice (deckSyncMode));
            cachedGridKey[(size_t) i] = gridKey;
        }
        const auto& beatGrid = cachedBeatGrid[(size_t) i];
        const bool hostTempoSync = getBoolParam ("hostSync") && syncSnapshot.bpm > 1.0;

        // Harmonic-match key: read the two scalars directly instead of copying the
        // whole TrackAnalysis (which carries beat-grid vectors) every block.
        int  keyPc = -1;
        bool keyHas = false;
        if (getBoolParam (ids.liveKey))
        {
            const int   pc   = liveKeyPitchClass[(size_t) i].load (std::memory_order_relaxed);
            const float conf = liveKeyConfidence[(size_t) i].load (std::memory_order_relaxed);
            if (pc >= 0 && pc < 12 && conf > 0.0f) { keyHas = true; keyPc = pc; }
        }
        if (! keyHas && deckAudio && deckAudio->analysis.hasKey())
        {
            keyHas = true;
            keyPc  = deckAudio->analysis.keyPitchClass;
        }
        const int masterKeyChoice = (int) std::round (getFloatParam ("masterKey"));
        const int deckKeyChoice = (int) std::round (getFloatParam (ids.keyTarget));
        const int targetKey = keyTargetChoiceToPitchClass (masterKeyChoice > 0 ? masterKeyChoice : deckKeyChoice);
        const float matchShift = keyHas && targetKey >= 0
                               ? (float) TrackAnalyzer::nearestSemitoneDelta (keyPc, targetKey)
                               : 0.0f;
        const auto warpPlan = warpEngine.makePlan (syncSnapshot,
                                                   beatGrid,
                                                   deckAudio ? deckAudio->sourceSampleRate : currentSampleRate,
                                                   deckAudio ? (double) deckAudio->audio.getNumSamples() : 0.0,
                                                   getFloatParam (ids.pitch) + matchShift,
                                                   getBoolParam (ids.keyLock),
                                                   hostTempoSync && getBoolParam ("beatSync"),
                                                   /*wantStatus*/ false);

        std::array<float, DeckPlayer::numStemLanes> stemGains {};
        std::array<float, DeckPlayer::numStemLanes> stemPitch {};
        for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        {
            stemGains[(size_t) lane] = getFloatParam (ids.stemGain[(size_t) lane]);
            stemPitch[(size_t) lane] = (lane >= DeckPlayer::drumsStem && lane <= DeckPlayer::vocalsStem)
                                     ? getFloatParam (ids.stemPitch[(size_t) lane]) : 0.0f;
        }

        auto& deckBuffer = deckScratchBuffers[(size_t) i];
        deckBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        deckBuffer.clear();
        decks[(size_t) i].process (deckBuffer, 0, buffer.getNumSamples(), getBoolParam (ids.play), getFloatParam (ids.volume),
                                   getFloatParam (ids.fader),
                                   getFloatParam (ids.low), getFloatParam (ids.mid), getFloatParam (ids.high),
                                   getFloatParam (ids.filter), 1.0f, warpPlan.timePitch, stemGains, stemPitch,
                                   warpPlan.beatSync);
        // ── Real-time stem separation ──────────────────────────────────────────
        // Active when none of the four stem lanes (drums/bass/music/vocals)
        // have offline files loaded.  The full-mix signal is split into four
        // bands; per-lane gain knobs then independently attenuate each band
        // before they are re-mixed back together.
        {
            bool hasOfflineStems = false;
            for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
                hasOfflineStems = hasOfflineStems || decks[(size_t) i].getStemAudio (lane) != nullptr;

            if (! hasOfflineStems && deckBuffer.getNumSamples() > 0 && deckBuffer.getNumChannels() >= 2)
            {
                const int n = deckBuffer.getNumSamples();
                auto& drums  = rtssDrumsBuffers [(size_t) i];
                auto& bass   = rtssBassBuffers  [(size_t) i];
                auto& music  = rtssMusicBuffers [(size_t) i];
                auto& vocals = rtssVocalsBuffers[(size_t) i];

                // Resize scratch buffers if needed (e.g. after prepareToPlay with larger block)
                const int ch = deckBuffer.getNumChannels();
                if (drums.getNumSamples() < n)  drums .setSize (ch, n, false, true, true);
                if (bass.getNumSamples()  < n)  bass  .setSize (ch, n, false, true, true);
                if (music.getNumSamples() < n)  music .setSize (ch, n, false, true, true);
                if (vocals.getNumSamples()< n)  vocals.setSize (ch, n, false, true, true);

                deckRTSS[(size_t) i].process (
                    deckBuffer.getReadPointer (0), deckBuffer.getReadPointer (1), n,
                    drums .getWritePointer (0), drums .getWritePointer (ch > 1 ? 1 : 0),
                    bass  .getWritePointer (0), bass  .getWritePointer (ch > 1 ? 1 : 0),
                    music .getWritePointer (0), music .getWritePointer (ch > 1 ? 1 : 0),
                    vocals.getWritePointer (0), vocals.getWritePointer (ch > 1 ? 1 : 0));

                // Gain from stem knobs (same param ids as the loaded-file path)
                const float gDrums  = juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 12.0f, getFloatParam (ids.stemGain[(size_t) DeckPlayer::drumsStem])));
                const float gBass   = juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 12.0f, getFloatParam (ids.stemGain[(size_t) DeckPlayer::bassStem])));
                const float gMusic  = juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 12.0f, getFloatParam (ids.stemGain[(size_t) DeckPlayer::musicStem])));
                const float gVocals = juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 12.0f, getFloatParam (ids.stemGain[(size_t) DeckPlayer::vocalsStem])));
                const float gFull   = juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 12.0f, getFloatParam (ids.stemGain[(size_t) DeckPlayer::fullMixStem])));

                // Per-stem FX: independent filter + space/echo send per stem.
                // stemFx index 0..3 maps to drums/bass/music/vocals (lanes 1..4).
                juce::AudioBuffer<float>* stemBufs[4] = { &drums, &bass, &music, &vocals };
                bool anyStemFx = false;
                for (int s = 0; s < 4; ++s)
                {
                    const int lane = DeckPlayer::drumsStem + s;
                    const float filt = getFloatParam (ids.stemFilt[(size_t) lane]);
                    const float send = getFloatParam (ids.stemSend[(size_t) lane]);
                    if (std::abs (filt - 0.5f) > 0.005f || send > 0.0005f)
                    {
                        anyStemFx = true;
                        // Wrap the stem's used region so the FX processes exactly n samples.
                        juce::AudioBuffer<float> view (stemBufs[s]->getArrayOfWritePointers(), ch, n);
                        stemFx[(size_t) i][(size_t) s].process (view, filt, send);
                    }
                }

                // Any stem knob differs from 0 dB? → remix separated stems
                const bool anyStemMoved = (gDrums  < 0.999f || gDrums  > 1.001f)
                                       || (gBass   < 0.999f || gBass   > 1.001f)
                                       || (gMusic  < 0.999f || gMusic  > 1.001f)
                                       || (gVocals < 0.999f || gVocals > 1.001f);

                if (anyStemMoved || anyStemFx)
                {
                    deckBuffer.clear (0, n);
                    for (int c = 0; c < ch; ++c)
                    {
                        deckBuffer.addFrom (c, 0, drums,  c, 0, n, gDrums  * gFull);
                        deckBuffer.addFrom (c, 0, bass,   c, 0, n, gBass   * gFull);
                        deckBuffer.addFrom (c, 0, music,  c, 0, n, gMusic  * gFull);
                        deckBuffer.addFrom (c, 0, vocals, c, 0, n, gVocals * gFull);
                    }
                }
            }
        }

        // Apply built-in FX before plugin chain
        deckFX[(size_t) i].process (deckBuffer,
                                    getFloatParam (ids.revMix),
                                    getFloatParam (ids.echoMix));
        hostGraph.processChain ((HostGraph::Chain) i, deckBuffer, midi);
        deckRecorders[(size_t) i].captureBlock (deckBuffer);

        // Per-deck peak (pre-crossfader) for the console channel meters.
        for (int ch = 0; ch < 2 && ch < deckBuffer.getNumChannels(); ++ch)
        {
            const float blockPeak = deckBuffer.getMagnitude (ch, 0, deckBuffer.getNumSamples());
            const float previous  = deckPeak[(size_t) i][(size_t) ch].load (std::memory_order_relaxed);
            deckPeak[(size_t) i][(size_t) ch].store (juce::jmax (blockPeak, previous * 0.86f), std::memory_order_relaxed);
        }

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, 0, deckBuffer, ch, 0, buffer.getNumSamples(), deckMixGain);
    }

    if (micCaptured)
    {
        hostGraph.processChain (HostGraph::Chain::mic, micInput, midi);
        const float micGain = juce::Decibels::decibelsToGain (getFloatParam ("micGain"));

        // Talkover ducking: measure mic peak, smooth it, duck the deck output
        if (talkoverEnabled.load (std::memory_order_relaxed))
        {
            float micPeak = 0.0f;
            for (int ch = 0; ch < micInput.getNumChannels(); ++ch)
                for (int s = 0; s < micInput.getNumSamples(); ++s)
                    micPeak = juce::jmax (micPeak, std::abs (micInput.getSample (ch, s)));

            micPeak *= micGain;
            const float thresh    = juce::Decibels::decibelsToGain (talkoverThresholdDb.load (std::memory_order_relaxed));
            const float duckGain  = juce::Decibels::decibelsToGain (talkoverDuckDb.load (std::memory_order_relaxed));
            const float target    = micPeak > thresh ? duckGain : 1.0f;
            // Attack 5 ms, release 150 ms at 44.1k (block-rate smoothing)
            const float n = (float) buffer.getNumSamples();
            const float alpha = target < talkoverDuckGain ? (1.0f - std::exp (-n / (0.005f * (float) currentSampleRate)))
                                                           : (1.0f - std::exp (-n / (0.150f * (float) currentSampleRate)));
            talkoverDuckGain += alpha * (target - talkoverDuckGain);
            buffer.applyGain (talkoverDuckGain);
        }

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, 0, micInput, juce::jmin (ch, micInput.getNumChannels() - 1), 0, buffer.getNumSamples(), micGain);
    }
    else if (isCapturingPerformance && getTotalNumInputChannels() == 0)
    {
        micInput.setSize (juce::jmax (1, getTotalNumOutputChannels()), buffer.getNumSamples(), false, false, true);
        micInput.clear();
        micCaptured = true;   // capture silence so the mic recorder stays in sync
    }

    std::array<float, PadBank::numPads> padGains {};
    for (int i = 0; i < PadBank::numPads; ++i)
        padGains[(size_t) i] = getFloatParam ("pad" + juce::String (i + 1) + "Gain");
    padBank.process (buffer, 0, buffer.getNumSamples(), padGains);

    // Arrangement + piano-roll MIDI share one transport (the arrangement playhead),
    // mixing into the master bus (subject to master gain + limiter).
    const bool arrPlaying = arrangementPlayer.isPlaying();
    if (arrPlaying)
    {
        const juce::int64 arrPos = arrangementPlayer.getPositionSamples();
        arrangementPlayer.process (buffer);                          // audio clips + advance playhead
        if (! usingTeMidi.load (std::memory_order_relaxed))
            midiClipPlayer.process (buffer, arrPos, buffer.getNumSamples());
        metronome.process (buffer, arrPos, buffer.getNumSamples());  // beat click
        if (arrangementPlayer.getPositionSamples() < arrPos && ! usingTeMidi.load (std::memory_order_relaxed))
            midiClipPlayer.allNotesOff();
    }
    else
    {
        arrangementPlayer.process (buffer);                          // no-op while stopped
        if (arrWasPlaying)
            midiClipPlayer.allNotesOff();
    }
    // When arrangementPlayer auto-stops (reached end), mirror that to TE transport
    if (arrWasPlaying && ! arrPlaying)
    {
        usingTeMidi.store (false, std::memory_order_relaxed);
        if (teWrapper != nullptr)
            teStopRequested.store (true, std::memory_order_relaxed);  // serviced on the message thread
    }
    arrWasPlaying = arrPlaying;

    // Tracktion Engine tracks (arrangement layer — additive to the main mix).
    // Reuses pre-allocated scratch buffers — no per-block heap allocation.
    if (teWrapper != nullptr)
    {
        teWrapper->playheadSync.synchronise (*this);
        teMidiScratch.clear();
        teScratch.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        teScratch.clear();
        teWrapper->audioInterface.processBlock (teScratch, teMidiScratch);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, 0, teScratch, ch, 0, buffer.getNumSamples());
    }

    hostGraph.processChain (HostGraph::Chain::master, buffer, midi);
    masterFx.process (buffer, getFloatParam ("masterFxX"), getFloatParam ("masterFxY"));
    buffer.applyGain (juce::Decibels::decibelsToGain (getFloatParam ("master")));
    // AI mastering assistant sits after user trim, before the safety limiter.
    smartMaster.process (buffer,
                         (int) std::round (getFloatParam ("smartMasterMode")),
                         getBoolParam ("smartMasterOn") ? getFloatParam ("smartMasterAmount") : 0.0f);
    applyLimiter (buffer, getFloatParam ("limiterCeiling"));

    // Master peak meters (block peak with fall-back decay).
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        const float blockPeak = buffer.getMagnitude (ch, 0, buffer.getNumSamples());
        const float previous  = masterPeak[(size_t) ch].load (std::memory_order_relaxed);
        masterPeak[(size_t) ch].store (juce::jmax (blockPeak, previous * 0.86f), std::memory_order_relaxed);
    }

    masterRecorder.captureBlock (buffer);
    if (micCaptured)
        micRecorder.captureBlock (micInput);
    if (isCapturingPerformance)
        recordingSampleCounter.fetch_add (buffer.getNumSamples(), std::memory_order_relaxed);
    transportSampleCounter += buffer.getNumSamples();

    // Motorized-fader feedback: emit CC out for mapped faders whose value changed
    // (so hardware/LED controllers track the UI). Change-detection avoids loops.
    midiMapping.writeFeedback (midi, 0);
}

void MainPluginProcessor::applyLimiter (juce::AudioBuffer<float>& buffer, float ceilingDb)
{
    const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

        const float targetGain = peak > ceiling ? ceiling / (peak + 1.0e-9f) : 1.0f;
        limiterEnvelope = targetGain < limiterEnvelope ? targetGain : limiterEnvelope + 0.002f * (1.0f - limiterEnvelope);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, juce::jlimit (-0.995f, 0.995f, buffer.getSample (ch, i) * limiterEnvelope));
    }
}

juce::String MainPluginProcessor::deckPathProperty (int deckIndex)
{
    return deckPrefix (deckIndex) + "File";
}

juce::String MainPluginProcessor::deckStemPathProperty (int deckIndex, int stemLaneIndex)
{
    if (stemLaneIndex == DeckPlayer::fullMixStem)
        return deckPathProperty (deckIndex);

    return deckPrefix (deckIndex) + "Stem" + DeckPlayer::stemLaneId (stemLaneIndex) + "File";
}

juce::String MainPluginProcessor::padPathProperty (int padIndex)
{
    return "pad" + juce::String (padIndex + 1) + "File";
}

void MainPluginProcessor::setStatePathProperty (const juce::Identifier& property, const juce::String& path)
{
    apvts.state.setProperty (property, path, nullptr);
}

void MainPluginProcessor::applyPremiumStemMix (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return;

    const auto deck = deckPrefix (deckIndex);
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
    {
        const auto paramId = deck + "Stem" + DeckPlayer::stemLaneId (lane) + "Gain";
        if (auto* gain = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (paramId)))
        {
            const float targetDb = lane == DeckPlayer::fullMixStem ? -60.0f : 0.0f;
            gain->setValueNotifyingHost (gain->convertTo0to1 (targetDb));
        }
    }
}

std::shared_ptr<const DeckPlayer::LoadedAudio> MainPluginProcessor::loadAudioFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (! reader || reader->lengthInSamples <= 0)
        return {};

    auto loaded = std::make_shared<DeckPlayer::LoadedAudio>();
    loaded->audio.setSize ((int) juce::jmin<int64> (reader->numChannels, 2), (int) reader->lengthInSamples);
    reader->read (&loaded->audio, 0, (int) reader->lengthInSamples, 0, true, true);
    loaded->sourceSampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    loaded->path = file.getFullPathName();
    loaded->displayName = file.getFileName();
    loaded->analysis = TrackAnalyzer::analyze (loaded->audio, loaded->sourceSampleRate);
    return loaded;
}

void MainPluginProcessor::loadAudioAsync (const juce::File& file, std::function<void(std::shared_ptr<const DeckPlayer::LoadedAudio>)> onLoaded)
{
    if (! file.existsAsFile())
        return;

    loadPool.addJob (new AudioLoadJob (file,
        [this] (const juce::File& f) { return loadAudioFile (f); },
        std::move (onLoaded)), true);
}

void MainPluginProcessor::loadDeckFileAsync (int deckIndex, const juce::File& file)
{
    loadDeckStemFileAsync (deckIndex, DeckPlayer::fullMixStem, file);
}

void MainPluginProcessor::clearDeck (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return;
    decks[(size_t) deckIndex].reset();
    setStatePathProperty (deckPathProperty (deckIndex), {});
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        setStatePathProperty (deckStemPathProperty (deckIndex, lane), {});
    logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed),
                         "track_cleared", deckNameFor (deckIndex), "full_mix", "cleared");
}

void MainPluginProcessor::requestDeckAiStemsAsync (int deckIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return;

    const auto path = getDeckPath (deckIndex);
    if (path.isEmpty())
        return;

    const juce::File source (path);
    stemProvider.requestStemsAsync (source, [this, deckIndex] (StemProvider::Result result)
    {
        logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed),
                             "ai_stem_split", deckNameFor (deckIndex), "premium_offline_cache",
                             result.allCoreStemsReady ? "ready" : "failed_or_pending",
                             result.cacheDirectory.getFullPathName(), result.message);

        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
            if (result.stems[(size_t) lane].ready)
                loadDeckStemFileAsync (deckIndex, lane, result.stems[(size_t) lane].file);

        if (result.allCoreStemsReady)
            applyPremiumStemMix (deckIndex);
    });
}

void MainPluginProcessor::loadDeckStemFileAsync (int deckIndex, int stemLaneIndex, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks) || ! juce::isPositiveAndBelow (stemLaneIndex, DeckPlayer::numStemLanes))
        return;

    loadAudioAsync (file, [this, deckIndex, stemLaneIndex, path = file.getFullPathName()] (std::shared_ptr<const DeckPlayer::LoadedAudio> audio)
    {
        if (! audio)
            return;
        decks[(size_t) deckIndex].setStemAudio (stemLaneIndex, audio);
        if (stemLaneIndex == DeckPlayer::fullMixStem)
        {
            decks[(size_t) deckIndex].seekStart();
            if (audio->analysis.detectedBpm > 1.0f)
                if (auto* bpm = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (deckPrefix (deckIndex) + "Bpm")))
                    bpm->setValueNotifyingHost (bpm->convertTo0to1 (juce::jlimit (40.0f, 240.0f, audio->analysis.detectedBpm)));

            stemProvider.requestStemsAsync (juce::File (path), [this, deckIndex] (StemProvider::Result result)
            {
                logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed),
                                     "stem_cache", deckNameFor (deckIndex), "offline_cache",
                                     result.allCoreStemsReady ? "ready" : "pending",
                                     result.cacheDirectory.getFullPathName(), result.message);

                for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
                    if (result.stems[(size_t) lane].ready)
                        loadDeckStemFileAsync (deckIndex, lane, result.stems[(size_t) lane].file);

                if (result.allCoreStemsReady)
                    applyPremiumStemMix (deckIndex);
            });
        }
        setStatePathProperty (deckStemPathProperty (deckIndex, stemLaneIndex), path);
        logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed), "track_loaded",
                             deckNameFor (deckIndex), DeckPlayer::stemLaneId (stemLaneIndex), audio->displayName,
                             path, "Audio loaded into " + DeckPlayer::stemLaneLabel (stemLaneIndex));
    });
}

void MainPluginProcessor::loadPadFileAsync (int padIndex, const juce::File& file)
{
    loadAudioAsync (file, [this, padIndex, path = file.getFullPathName()] (std::shared_ptr<const DeckPlayer::LoadedAudio> audio)
    {
        if (! audio)
            return;
        padBank.setPadAudio (padIndex, audio);
        setStatePathProperty (padPathProperty (padIndex), path);
    });
}

void MainPluginProcessor::triggerPad (int padIndex)
{
    padBank.triggerPad (padIndex);
    logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed), "pad_trigger", {}, "pad", juce::String (padIndex + 1), getPadPath (padIndex));
}

void MainPluginProcessor::rewindDeck (int deckIndex)
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
    {
        decks[(size_t) deckIndex].seekStart();
        logPerformanceEvent (recordingSampleCounter.load (std::memory_order_relaxed), "cue_jump",
                             deckNameFor (deckIndex), "cue", "start", getDeckPath (deckIndex));
    }
}

// ── RTSS ─────────────────────────────────────────────────────────────────────
bool MainPluginProcessor::isDeckRtssActive (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return false;
    for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
        if (decks[(size_t) deckIndex].getStemAudio (lane) != nullptr)
            return false;
    return decks[(size_t) deckIndex].getLoadedAudio() != nullptr;
}

bool MainPluginProcessor::deckHasNeuralStems (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return false;
    for (int lane = DeckPlayer::drumsStem; lane <= DeckPlayer::vocalsStem; ++lane)
        if (decks[(size_t) deckIndex].getStemAudio (lane) != nullptr)
            return true;
    return false;
}

void MainPluginProcessor::setDeckRtssSensitivity (int deckIndex, float sensitivity)
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        deckRTSS[(size_t) deckIndex].setDrumsSensitivity (sensitivity);
}

// ── Arrangement playback ──────────────────────────────────────────────────────
void MainPluginProcessor::buildAndStartArrangement (std::vector<ArrangementClipSpec> clips, double minLengthSeconds, double startSeconds)
{
    // Stop both transports before reconfiguring
    arrangementPlayer.stop();
    if (teWrapper != nullptr)
        teWrapper->edit->getTransport().stop (false, false);

    // Compute total arrangement length
    double totalLen = minLengthSeconds;
    for (const auto& spec : clips)
        totalLen = juce::jmax (totalLen, spec.timelineStartSeconds + spec.lengthSeconds);

    const bool hasContent = ! clips.empty() || totalLen > 0.0;
    if (! hasContent)
        return;

    // Snapshot MIDI data (pending* members are message-thread only; safe here)
    auto capturedNotes = pendingMidiNotes;
    double capturedBpm = pendingMidiBpm;

    // All TE operations require the message thread; capture by value for async safety.
    struct TeArrangementPayload
    {
        std::vector<ArrangementClipSpec>      clips;
        std::vector<MidiClipPlayer::SourceNote> notes;
        double totalLen   = 0.0;
        double startSecs  = 0.0;
        double midiBpm    = 120.0;
    };
    auto payload = std::make_shared<TeArrangementPayload>();
    payload->clips     = std::move (clips);
    payload->notes     = std::move (capturedNotes);
    payload->totalLen  = totalLen;
    payload->startSecs = startSeconds;
    payload->midiBpm   = capturedBpm;

    juce::MessageManager::callAsync ([this, payload]
    {
        if (teWrapper == nullptr) return;

        auto& edit = *teWrapper->edit;
        const auto& capturedClips = payload->clips;
        const auto& midiNotes     = payload->notes;
        const double arrTotalLen  = payload->totalLen;
        const double arrStart     = payload->startSecs;
        const double midiBpm      = payload->midiBpm;

        // ── Rebuild TE tracks: numDecks audio + numMidiLanes synth ───────────
        // Clear existing clips on every track before re-populating.
        for (auto* t : tracktion::getAudioTracks (edit))
        {
            auto tc = t->getClips();
            for (int i = tc.size(); --i >= 0;)
                tc.getUnchecked (i)->removeFromParent();
        }

        const int totalTracks = numDecks + MidiClipPlayer::numLanes;
        edit.ensureNumberOfAudioTracks (totalTracks);
        auto allTracks = tracktion::getAudioTracks (edit);

        // ── Audio deck tracks (lanes 0..numDecks-1) ──────────────────────────
        for (const auto& spec : capturedClips)
        {
            if (! spec.file.existsAsFile() || spec.lengthSeconds <= 0.0) continue;
            const int lane = juce::jlimit (0, numDecks - 1, spec.lane);
            auto* track = allTracks[lane];

            const auto tStart = tracktion::TimePosition::fromSeconds (spec.timelineStartSeconds);
            const auto tEnd   = tracktion::TimePosition::fromSeconds (spec.timelineStartSeconds + spec.lengthSeconds);
            const auto srcOff = tracktion::TimeDuration::fromSeconds (spec.sourceStartSeconds);

            if (auto wclip = track->insertWaveClip (
                    spec.file.getFileNameWithoutExtension(), spec.file,
                    tracktion::ClipPosition { tracktion::TimeRange (tStart, tEnd), srcOff },
                    false))
            {
                if (spec.gain != 1.0f)
                    wclip->setGainDB (juce::Decibels::gainToDecibels (spec.gain));
                if (spec.fadeInSeconds  > 0.0)
                    wclip->setFadeIn  (tracktion::TimeDuration::fromSeconds (spec.fadeInSeconds));
                if (spec.fadeOutSeconds > 0.0)
                    wclip->setFadeOut (tracktion::TimeDuration::fromSeconds (spec.fadeOutSeconds));
            }
        }

        // ── MIDI synth tracks (lanes numDecks..numDecks+numMidiLanes-1) ──────
        if (arrTotalLen > 0.0 && ! midiNotes.empty())
        {
            const tracktion::TimeRange arrangementRange {
                tracktion::TimePosition::fromSeconds (0.0),
                tracktion::TimePosition::fromSeconds (arrTotalLen)
            };

            for (int midiLane = 0; midiLane < MidiClipPlayer::numLanes; ++midiLane)
            {
                auto* midiSynthTrack = allTracks[numDecks + midiLane];

                // Ensure FourOsc synth plugin is on this track
                bool has4osc = false;
                for (auto* p : midiSynthTrack->pluginList.getPlugins())
                    if (p->getPluginType() == tracktion::FourOscPlugin::xmlTypeName)
                        { has4osc = true; break; }
                if (! has4osc)
                    if (auto synth = edit.getPluginCache().createNewPlugin (tracktion::FourOscPlugin::xmlTypeName, {}))
                        midiSynthTrack->pluginList.insertPlugin (synth, 0, nullptr);

                // Insert a MIDI clip spanning the full arrangement
                if (auto mclip = midiSynthTrack->insertMIDIClip ("MIDI " + juce::String (midiLane), arrangementRange, nullptr))
                {
                    auto& seq = mclip->getSequence();
                    seq.clear (nullptr);

                    for (const auto& note : midiNotes)
                    {
                        if (note.lane != midiLane) continue;
                        seq.addNote (note.pitch,
                                     tracktion::BeatPosition::fromBeats (note.startBeats),
                                     tracktion::BeatDuration::fromBeats (note.lengthBeats),
                                     note.velocity, 0, nullptr);
                    }
                }
            }
            usingTeMidi.store (true, std::memory_order_relaxed);
        }
        else
        {
            usingTeMidi.store (false, std::memory_order_relaxed);
        }

        // ── Sync master BPM to TE tempo sequence ────────────────────────────
        const double bpm = juce::jlimit (40.0, 240.0, (double) getFloatParam ("masterBpm"));
        if (edit.tempoSequence.getNumTempos() > 0)
            edit.tempoSequence.getTempoAt (tracktion::TimePosition()).setBpm (bpm);

        // ── Start TE transport ───────────────────────────────────────────────
        auto& transport = edit.getTransport();
        transport.setPosition (tracktion::TimePosition::fromSeconds (arrStart));
        transport.ensureContextAllocated (true);
        transport.play (false);

        // ── arrangementPlayer as lightweight position tracker ────────────────
        // Null schedule → no audio output; it only advances the playhead so
        // MidiClipPlayer and metronome know where we are in the arrangement.
        arrangementPlayer.setSchedule (nullptr);
        arrangementPlayer.setLengthSeconds (arrTotalLen);
        arrangementPlayer.setPositionSeconds (arrStart);
        arrangementPlayer.start();
    });
}

void MainPluginProcessor::setMidiNotes (const std::vector<MidiClipPlayer::SourceNote>& notes, double bpm)
{
    pendingMidiNotes = notes;
    pendingMidiBpm   = bpm;
    midiClipPlayer.setNotes (notes, bpm);   // keep non-TE path in sync
}

void MainPluginProcessor::setTeArrangementLaneGain (int lane, float gainLinear)
{
    const float dB = juce::Decibels::gainToDecibels (gainLinear);
    juce::MessageManager::callAsync ([this, lane, dB] {
        if (teWrapper == nullptr) return;
        auto tracks = tracktion::getAudioTracks (*teWrapper->edit);
        if (juce::isPositiveAndBelow (lane, tracks.size()))
            if (auto* vol = tracks[lane]->getVolumePlugin())
                vol->setVolumeDb (dB);
    });
}

void MainPluginProcessor::stopArrangement()
{
    usingTeMidi.store (false, std::memory_order_relaxed);
    arrangementPlayer.stop();
    if (teWrapper != nullptr)
        teWrapper->edit->getTransport().stop (false, false);
}

void MainPluginProcessor::serviceDeferredActions()
{
    // Message thread only. The audio thread requests a TE transport stop via an
    // atomic (instead of allocating a callAsync closure in processBlock).
    if (teStopRequested.exchange (false, std::memory_order_relaxed))
        if (teWrapper != nullptr)
            teWrapper->edit->getTransport().stop (false, false);
}

void MainPluginProcessor::setArrangementPositionSeconds (double s)
{
    arrangementPlayer.setPositionSeconds (s);
    if (teWrapper != nullptr)
        teWrapper->edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (s));
}

void MainPluginProcessor::setArrangementLoop (double startSec, double endSec, bool enabled)
{
    arrangementPlayer.setLoop (startSec, endSec, enabled);
    if (teWrapper != nullptr)
    {
        auto& transport = teWrapper->edit->getTransport();
        transport.loopPoint1 = tracktion::TimePosition::fromSeconds (startSec);
        transport.loopPoint2 = tracktion::TimePosition::fromSeconds (endSec);
        transport.looping    = enabled;
    }
}

// ── Slip mode + loop roll ────────────────────────────────────────────────────
void MainPluginProcessor::setDeckSlipEnabled (int deckIndex, bool on) noexcept
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        decks[(size_t) deckIndex].setSlipEnabled (on);
}

bool MainPluginProcessor::isDeckSlipEnabled (int deckIndex) const noexcept
{
    return juce::isPositiveAndBelow (deckIndex, numDecks) && decks[(size_t) deckIndex].isSlipEnabled();
}

void MainPluginProcessor::beginDeckLoopRoll (int deckIndex, double lengthSeconds) noexcept
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        decks[(size_t) deckIndex].beginLoopRoll (lengthSeconds);
}

void MainPluginProcessor::endDeckLoopRoll (int deckIndex) noexcept
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        decks[(size_t) deckIndex].endLoopRoll();
}

// ── Seek / beat jump ────────────────────────────────────────────────────────
void MainPluginProcessor::seekDeckSeconds (int deckIndex, double seconds)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return;
    decks[(size_t) deckIndex].seekToSamples (seconds * audio->sourceSampleRate);
}

stemdeck::TransitionPlan MainPluginProcessor::planAutomixTransition (int outDeck, int inDeck) const
{
    stemdeck::TransitionPlan plan;
    if (! juce::isPositiveAndBelow (outDeck, numDecks) || ! juce::isPositiveAndBelow (inDeck, numDecks) || outDeck == inDeck)
        return plan;

    const auto aOut = getDeckActiveAnalysis (outDeck);
    const auto aIn  = getDeckActiveAnalysis (inDeck);
    const auto audioOut = decks[(size_t) outDeck].getLoadedAudio();
    const auto audioIn  = decks[(size_t) inDeck].getLoadedAudio();

    std::vector<float> eOut, eIn;
    if (audioOut != nullptr)
        eOut = stemdeck::AutomixEngine::phraseEnergies (audioOut->audio, aOut.sampleRate,
                    stemdeck::AutomixEngine::phraseBoundaries (aOut, 16));
    if (audioIn != nullptr)
        eIn = stemdeck::AutomixEngine::phraseEnergies (audioIn->audio, aIn.sampleRate,
                    stemdeck::AutomixEngine::phraseBoundaries (aIn, 16));

    return stemdeck::AutomixEngine::planTransition (aOut, aIn, eOut, eIn);
}

stemdeck::TransitionPlan MainPluginProcessor::beginAutomix (int outDeck, int inDeck)
{
    stemdeck::TransitionPlan plan = planAutomixTransition (outDeck, inDeck);
    if (! plan.valid)
        return plan;

    const auto audioIn = decks[(size_t) inDeck].getLoadedAudio();
    if (audioIn != nullptr && audioIn->sourceSampleRate > 0.0)
        decks[(size_t) inDeck].seekToSamples (plan.inPointSampleB);   // in-point is in source samples

    // Crossfader routing: outgoing on A/left, incoming on B/right.
    setDeckCfSide (outDeck, 0);
    setDeckCfSide (inDeck, 2);

    // Beat-sync so the incoming deck locks to the master grid.
    if (auto* p = apvts.getParameter ("beatSync")) p->setValueNotifyingHost (1.0f);

    // Start the incoming deck; the outgoing one keeps playing until the blend ends.
    if (auto* p = apvts.getParameter (deckPrefix (inDeck) + "Play")) p->setValueNotifyingHost (1.0f);

    return plan;
}

void MainPluginProcessor::beatJumpDeck (int deckIndex, double bars)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return;

    const float bpm = getFloatParam (deckPrefix (deckIndex) + "Bpm");
    const double barSeconds = (60.0 / juce::jmax (1.0f, bpm)) * 4.0;
    const double currentPos = getDeckPlayheadSeconds (deckIndex);
    const double newPos = juce::jmax (0.0, currentPos + bars * barSeconds);
    decks[(size_t) deckIndex].seekToSamples (newPos * audio->sourceSampleRate);
}

// ── Hot cues ────────────────────────────────────────────────────────────────
juce::String MainPluginProcessor::hotCueProperty (int deckIndex, int cueIndex)
{
    return deckPrefix (deckIndex) + "HotCue" + juce::String (cueIndex);
}

void MainPluginProcessor::setHotCue (int deckIndex, int cueIndex, double seconds)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    if (! juce::isPositiveAndBelow (cueIndex, numHotCues)) return;
    apvts.state.setProperty (hotCueProperty (deckIndex, cueIndex), seconds, nullptr);
}

void MainPluginProcessor::clearHotCue (int deckIndex, int cueIndex)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    if (! juce::isPositiveAndBelow (cueIndex, numHotCues)) return;
    apvts.state.setProperty (hotCueProperty (deckIndex, cueIndex), -1.0, nullptr);
}

double MainPluginProcessor::getHotCue (int deckIndex, int cueIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return -1.0;
    if (! juce::isPositiveAndBelow (cueIndex, numHotCues)) return -1.0;
    const auto val = apvts.state.getProperty (hotCueProperty (deckIndex, cueIndex), -1.0);
    return (double) val;
}

void MainPluginProcessor::jumpToHotCue (int deckIndex, int cueIndex)
{
    const double seconds = getHotCue (deckIndex, cueIndex);
    if (seconds >= 0.0)
        seekDeckSeconds (deckIndex, seconds);
}

// ── Intro / Outro markers ────────────────────────────────────────────────────
void MainPluginProcessor::setIntroEnd (int d, double s)
{
    if (juce::isPositiveAndBelow (d, numDecks))
        apvts.state.setProperty (deckPrefix (d) + "IntroEnd", s, nullptr);
}
double MainPluginProcessor::getIntroEnd (int d) const
{
    if (! juce::isPositiveAndBelow (d, numDecks)) return -1.0;
    return (double) apvts.state.getProperty (deckPrefix (d) + "IntroEnd", -1.0);
}
void MainPluginProcessor::clearIntroEnd (int d)   { setIntroEnd (d, -1.0); }

void MainPluginProcessor::setOutroStart (int d, double s)
{
    if (juce::isPositiveAndBelow (d, numDecks))
        apvts.state.setProperty (deckPrefix (d) + "OutroStart", s, nullptr);
}
double MainPluginProcessor::getOutroStart (int d) const
{
    if (! juce::isPositiveAndBelow (d, numDecks)) return -1.0;
    return (double) apvts.state.getProperty (deckPrefix (d) + "OutroStart", -1.0);
}
void MainPluginProcessor::clearOutroStart (int d) { setOutroStart (d, -1.0); }

// ── Crossfader side assignment ────────────────────────────────────────────────
void MainPluginProcessor::setDeckCfSide (int d, int side)
{
    if (juce::isPositiveAndBelow (d, numDecks))
        apvts.state.setProperty (deckPrefix (d) + "CfSide", juce::jlimit (0, 2, side), nullptr);
}

int MainPluginProcessor::getDeckCfSide (int d) const
{
    if (! juce::isPositiveAndBelow (d, numDecks)) return d % 2 == 0 ? 0 : 2;
    return (int) apvts.state.getProperty (deckPrefix (d) + "CfSide", d % 2 == 0 ? 0 : 2);
}

// ── Loop region ──────────────────────────────────────────────────────────────
void MainPluginProcessor::setDeckLoopSeconds (int deckIndex, double inSeconds, double outSeconds)
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return;
    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return;
    const double sr = audio->sourceSampleRate;
    decks[(size_t) deckIndex].setLoopRegion (inSeconds * sr, outSeconds * sr);
}

void MainPluginProcessor::setDeckLoopActive (int deckIndex, bool active)
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        decks[(size_t) deckIndex].setLoopActive (active);
}

void MainPluginProcessor::clearDeckLoop (int deckIndex)
{
    if (juce::isPositiveAndBelow (deckIndex, numDecks))
        decks[(size_t) deckIndex].clearLoopRegion();
}

bool MainPluginProcessor::isDeckLoopActive (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return false;
    return decks[(size_t) deckIndex].isLoopActive();
}

double MainPluginProcessor::getDeckLoopInSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return -1.0;
    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return -1.0;
    const double s = decks[(size_t) deckIndex].getLoopInSamples();
    return s < 0.0 ? -1.0 : s / audio->sourceSampleRate;
}

double MainPluginProcessor::getDeckLoopOutSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks)) return -1.0;
    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0) return -1.0;
    const double s = decks[(size_t) deckIndex].getLoopOutSamples();
    return s < 0.0 ? -1.0 : s / audio->sourceSampleRate;
}

// ── Tap tempo ────────────────────────────────────────────────────────────────
void MainPluginProcessor::tapTempo()
{
    const juce::int64 now = juce::Time::currentTimeMillis();
    // Drop taps older than 3 seconds.
    tapTimes.erase (std::remove_if (tapTimes.begin(), tapTimes.end(),
                                    [now] (juce::int64 t) { return (now - t) > 3000; }),
                    tapTimes.end());
    tapTimes.push_back (now);

    if (tapTimes.size() < 2) return;

    double avgInterval = 0.0;
    for (size_t i = 1; i < tapTimes.size(); ++i)
        avgInterval += (double) (tapTimes[i] - tapTimes[i - 1]);
    avgInterval /= (double) (tapTimes.size() - 1);

    const float bpm = (float) juce::jlimit (40.0, 240.0, 60000.0 / avgInterval);
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("masterBpm")))
        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (bpm));
}

bool MainPluginProcessor::beginMasterRecording (const juce::File& destinationFile)
{
    if (destinationFile.getFullPathName().isEmpty())
        return false;

    const auto micFile = withSuffix (destinationFile, "-mic");
    if (! masterRecorder.beginRecording (destinationFile))
        return false;

    if (! micRecorder.beginRecording (micFile))
    {
        masterRecorder.stopRecording();
        return false;
    }

    for (int i = 0; i < numDecks; ++i)
    {
        const auto deckFile = withSuffix (destinationFile, "-deck-" + juce::String::charToString ((juce_wchar) ('A' + i)));
        if (! deckRecorders[(size_t) i].beginRecording (deckFile))
        {
            masterRecorder.stopRecording();
            micRecorder.stopRecording();
            for (int j = 0; j < i; ++j)
                deckRecorders[(size_t) j].stopRecording();
            return false;
        }
        lastDeckRecordingTargets[(size_t) i] = deckFile;
    }

    recordingSampleCounter.store (0, std::memory_order_relaxed);
    performanceRecorder.begin (destinationFile, currentSampleRate);
    initialisePerformanceCaptureState();
    lastPerformanceEventLogFile = performanceRecorder.getEventLogFile();
    lastArrangementFile = performanceRecorder.getArrangementFile();
    return true;
}

juce::File MainPluginProcessor::stopMasterRecording()
{
    const int64_t endSample = recordingSampleCounter.load (std::memory_order_relaxed);
    const auto masterFile = masterRecorder.stopRecording();
    const auto micFile = micRecorder.stopRecording();
    std::array<juce::File, numDecks> deckFiles;
    for (int i = 0; i < numDecks; ++i)
        deckFiles[(size_t) i] = deckRecorders[(size_t) i].stopRecording();
    lastDeckRecordingTargets = deckFiles;
    lastArrangementFile = performanceRecorder.finishAndWrite (endSample, masterFile, micFile, deckFiles);
    lastPerformanceEventLogFile = performanceRecorder.getEventLogFile();
    return masterFile;
}

juce::File MainPluginProcessor::getLastDeckRecordingFile (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};
    return lastDeckRecordingTargets[(size_t) deckIndex];
}

juce::String MainPluginProcessor::getDeckPath (int deckIndex) const
{
    return juce::isPositiveAndBelow (deckIndex, numDecks) ? decks[(size_t) deckIndex].getPath() : juce::String{};
}

juce::String MainPluginProcessor::getDeckStemPath (int deckIndex, int stemLaneIndex) const
{
    return juce::isPositiveAndBelow (deckIndex, numDecks) && juce::isPositiveAndBelow (stemLaneIndex, DeckPlayer::numStemLanes)
        ? decks[(size_t) deckIndex].getStemPath (stemLaneIndex)
        : juce::String{};
}

double MainPluginProcessor::getDeckPlayheadSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return 0.0;
    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0)
        return 0.0;
    return decks[(size_t) deckIndex].getPlayheadReferenceSamples() / audio->sourceSampleRate;
}

double MainPluginProcessor::getDeckTotalSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return 0.0;
    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0)
        return 0.0;
    return (double) audio->audio.getNumSamples() / audio->sourceSampleRate;
}

juce::StringArray MainPluginProcessor::getStemLaneLabels() const
{
    juce::StringArray labels;
    for (int lane = 0; lane < DeckPlayer::numStemLanes; ++lane)
        labels.add (DeckPlayer::stemLaneLabel (lane));
    return labels;
}

juce::StringArray MainPluginProcessor::getKeyTargetChoices() const
{
    return keyTargetChoices();
}

juce::StringArray MainPluginProcessor::getTempoInterpretationChoices() const
{
    return BeatSyncEngine::tempoInterpretationChoices();
}

juce::StringArray MainPluginProcessor::getSyncModeChoices() const
{
    return BeatSyncEngine::syncModeChoices();
}

TrackAnalysis MainPluginProcessor::getDeckAnalysis (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    if (auto audio = decks[(size_t) deckIndex].getLoadedAudio())
        return audio->analysis;

    return {};
}

TrackAnalysis MainPluginProcessor::getDeckLiveAnalysisFromAtomics (int deckIndex) const
{
    TrackAnalysis analysis;
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return analysis;

    analysis.keyPitchClass = liveKeyPitchClass[(size_t) deckIndex].load (std::memory_order_relaxed);
    analysis.keyIsMinor = liveKeyIsMinor[(size_t) deckIndex].load (std::memory_order_relaxed) != 0;
    analysis.keyConfidence = liveKeyConfidence[(size_t) deckIndex].load (std::memory_order_relaxed);
    return analysis;
}

TrackAnalysis MainPluginProcessor::getDeckLiveAnalysis (int deckIndex) const
{
    return getDeckLiveAnalysisFromAtomics (deckIndex);
}

TrackAnalysis MainPluginProcessor::getDeckActiveAnalysis (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto deck = deckPrefix (deckIndex);
    auto live = getDeckLiveAnalysisFromAtomics (deckIndex);
    if (getBoolParam (deck + "LiveKey") && live.hasKey())
        return live;

    return getDeckAnalysis (deckIndex);
}

void MainPluginProcessor::refreshLiveKeyAnalysis()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();

    for (int i = 0; i < numDecks; ++i)
    {
        const auto deck = deckPrefix (i);
        if (! getBoolParam (deck + "LiveKey"))
            continue;

        if (now - (double) lastLiveKeyAnalysisMs[(size_t) i] < 950.0)
            continue;

        lastLiveKeyAnalysisMs[(size_t) i] = (int64_t) now;
        const auto analysis = decks[(size_t) i].analyzeFullMixAroundPlayhead (8.0);
        liveKeyPitchClass[(size_t) i].store (analysis.keyPitchClass, std::memory_order_relaxed);
        liveKeyIsMinor[(size_t) i].store (analysis.keyIsMinor ? 1 : 0, std::memory_order_relaxed);
        liveKeyConfidence[(size_t) i].store (analysis.keyConfidence, std::memory_order_relaxed);
    }
}

juce::String MainPluginProcessor::getDeckKeyInfo (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto deck = deckPrefix (deckIndex);
    const bool liveKeyEnabled = getBoolParam (deck + "LiveKey");
    const auto liveAnalysis = getDeckLiveAnalysisFromAtomics (deckIndex);
    const auto analysis = getDeckActiveAnalysis (deckIndex);
    const int masterKeyChoice = (int) std::round (getFloatParam ("masterKey"));
    const int deckKeyChoice = (int) std::round (getFloatParam (deck + "KeyTarget"));
    const int targetKey = keyTargetChoiceToPitchClass (masterKeyChoice > 0 ? masterKeyChoice : deckKeyChoice);
    const float manualShift = getFloatParam (deck + "Pitch");

    juce::String text = analysis.hasKey()
        ? juce::String (liveKeyEnabled && liveAnalysis.hasKey() ? "Live key: " : "Key: ") + analysis.keyName() + " (" + juce::String ((int) std::round (analysis.keyConfidence * 100.0f)) + "%)"
        : "Key: unknown";

    if (analysis.hasKey() && targetKey >= 0)
    {
        const int matchShift = TrackAnalyzer::nearestSemitoneDelta (analysis.keyPitchClass, targetKey);
        text += u8 (" \xe2\x86\x92 ") + TrackAnalyzer::pitchClassName (targetKey) + " (" + (matchShift >= 0 ? "+" : "") + juce::String (matchShift) + " st)";
    }

    if (std::abs (manualShift) > 0.01f)
        text += " + manual " + juce::String (manualShift, 1) + " st";

    if (getBoolParam (deck + "KeyLock"))
        text += u8 (" \xe2\x80\xa2 preserve requested");

    if (liveKeyEnabled && ! liveAnalysis.hasKey())
        text += u8 (" \xe2\x80\xa2 live listening\xe2\x80\xa6");

    return text;
}

juce::String MainPluginProcessor::getDeckTimePitchStatus (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto deck = deckPrefix (deckIndex);
    const auto analysis = getDeckActiveAnalysis (deckIndex);
    const int masterKeyChoice = (int) std::round (getFloatParam ("masterKey"));
    const int deckKeyChoice = (int) std::round (getFloatParam (deck + "KeyTarget"));
    const int targetKey = keyTargetChoiceToPitchClass (masterKeyChoice > 0 ? masterKeyChoice : deckKeyChoice);
    const float matchShift = analysis.hasKey() && targetKey >= 0
                           ? (float) TrackAnalyzer::nearestSemitoneDelta (analysis.keyPitchClass, targetKey)
                           : 0.0f;

    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    const auto grid = BeatSyncEngine::makeGrid (getDeckAnalysis (deckIndex),
                                                getFloatParam (deck + "Bpm"),
                                                getFloatParam (deck + "BeatOffset"),
                                                getFloatParam (deck + "Downbeat"),
                                                BeatSyncEngine::tempoInterpretationFromChoice ((int) std::round (getFloatParam (deck + "TempoInterpretation"))),
                                                BeatSyncEngine::syncModeFromChoice ((int) std::round (getFloatParam (deck + "SyncMode"))));
    const auto plan = warpEngine.makePlan (sync.getSnapshot(), grid,
                                           audio ? audio->sourceSampleRate : currentSampleRate,
                                           audio ? (double) audio->audio.getNumSamples() : 0.0,
                                           getFloatParam (deck + "Pitch") + matchShift,
                                           getBoolParam (deck + "KeyLock"),
                                           getBoolParam ("hostSync") && getBoolParam ("beatSync"));
    return plan.statusLabel;
}

juce::String MainPluginProcessor::getDeckBeatSyncStatus (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    const auto analysis = audio ? audio->analysis : TrackAnalysis{};
    const auto deck = deckPrefix (deckIndex);
    const auto grid = BeatSyncEngine::makeGrid (analysis,
                                                getFloatParam (deck + "Bpm"),
                                                getFloatParam (deck + "BeatOffset"),
                                                getFloatParam (deck + "Downbeat"),
                                                BeatSyncEngine::tempoInterpretationFromChoice ((int) std::round (getFloatParam (deck + "TempoInterpretation"))),
                                                BeatSyncEngine::syncModeFromChoice ((int) std::round (getFloatParam (deck + "SyncMode"))));
    const auto transport = sync.getSnapshot();
    const auto plan = BeatSyncEngine::makePlaybackPlan (transport, grid,
                                                        audio ? audio->sourceSampleRate : currentSampleRate,
                                                        audio ? (double) audio->audio.getNumSamples() : 0.0,
                                                        getBoolParam ("hostSync") && getBoolParam ("beatSync"));

    juce::String text = plan.statusLabel;
    if (analysis.detectedBpm > 1.0f)
        text += u8 (" \xe2\x80\xa2 detected ") + juce::String (analysis.detectedBpm, 1);
    if (! analysis.beatGridSamples.empty())
        text += u8 (" \xe2\x80\xa2 ") + juce::String ((int) analysis.beatGridSamples.size()) + " beats";
    if (! analysis.transientAnchorSamples.empty())
        text += u8 (" \xe2\x80\xa2 ") + juce::String ((int) analysis.transientAnchorSamples.size()) + " anchors";
    return text;
}

std::vector<double> MainPluginProcessor::getDeckBeatGridSeconds (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto audio = decks[(size_t) deckIndex].getLoadedAudio();
    if (audio == nullptr || audio->sourceSampleRate <= 0.0)
        return {};

    const auto deck = deckPrefix (deckIndex);
    const auto grid = BeatSyncEngine::makeGrid (audio->analysis,
                                                getFloatParam (deck + "Bpm"),
                                                getFloatParam (deck + "BeatOffset"),
                                                getFloatParam (deck + "Downbeat"),
                                                BeatSyncEngine::tempoInterpretationFromChoice ((int) std::round (getFloatParam (deck + "TempoInterpretation"))),
                                                BeatSyncEngine::syncModeFromChoice ((int) std::round (getFloatParam (deck + "SyncMode"))));

    std::vector<double> seconds;
    seconds.reserve (grid.beatGridSamples.size());
    for (const auto s : grid.beatGridSamples)
        seconds.push_back (s / audio->sourceSampleRate);
    return seconds;
}

juce::String MainPluginProcessor::getWarpEngineStatus() const
{
    return warpEngine.getStatusSummary();
}

juce::StringArray MainPluginProcessor::getSupportedPluginHostFormats() const
{
    return hostGraph.getSupportedPluginFormats();
}

juce::String MainPluginProcessor::getPluginHostStatus() const
{
    return hostGraph.getStatusSummary();
}

juce::String MainPluginProcessor::getStemProviderStatus() const
{
    return stemProvider.getName() + u8 (" \xe2\x80\xa2 cache: ") + stemProvider.getCacheRoot().getFullPathName();
}

juce::String MainPluginProcessor::getDeckStemProviderStatus (int deckIndex) const
{
    if (! juce::isPositiveAndBelow (deckIndex, numDecks))
        return {};

    const auto path = getDeckPath (deckIndex);
    const auto status = stemProvider.getStatusForSource (juce::File (path));
    juce::String text = status.headline;
    if (status.sourceFile.existsAsFile())
        text += u8 (" \xe2\x80\xa2 ") + juce::String (status.readyCoreStems) + "/" + juce::String (status.totalCoreStems) + " stems";
    if (status.jobQueued && ! status.jobRunning)
        text += u8 (" \xe2\x80\xa2 queued");
    if (status.jobRunning)
        text += u8 (" \xe2\x80\xa2 processing");
    if (status.detail.isNotEmpty())
        text += u8 (" \xe2\x80\xa2 ") + status.detail;
    return text;
}

juce::Array<juce::PluginDescription> MainPluginProcessor::getKnownHostPlugins() const
{
    return hostGraph.getKnownPlugins();
}

juce::File MainPluginProcessor::getPluginHostCacheFile() const
{
    return hostGraph.getCacheFile();
}

bool MainPluginProcessor::loadPluginHostCache()
{
    return hostGraph.loadCache();
}

bool MainPluginProcessor::savePluginHostCache() const
{
    return hostGraph.saveCache();
}

HostGraph::ScanResult MainPluginProcessor::scanPluginHostDefaultLocations (int maxPluginsPerFormat, bool recursive)
{
    return hostGraph.scanDefaultLocations (maxPluginsPerFormat, recursive);
}

juce::StringArray MainPluginProcessor::getPluginHostChainNames() const
{
    juce::StringArray names;
    for (int i = 0; i < (int) HostGraph::Chain::count; ++i)
        names.add (HostGraph::chainName ((HostGraph::Chain) i));
    return names;
}

void MainPluginProcessor::loadHostPluginToChainAsync (int chainIndex, int pluginIndex, HostGraph::LoadCallback callback)
{
    if (! juce::isPositiveAndBelow (chainIndex, (int) HostGraph::Chain::count))
    {
        if (callback)
            callback (false, "Invalid plugin chain");
        return;
    }

    const auto plugins = hostGraph.getKnownPlugins();
    if (! juce::isPositiveAndBelow (pluginIndex, plugins.size()))
    {
        if (callback)
            callback (false, "Invalid plugin selection");
        return;
    }

    hostGraph.loadPluginToChainAsync ((HostGraph::Chain) chainIndex, plugins.getReference (pluginIndex), std::move (callback));
}

void MainPluginProcessor::clearHostPluginChain (int chainIndex)
{
    if (juce::isPositiveAndBelow (chainIndex, (int) HostGraph::Chain::count))
        hostGraph.clearChain ((HostGraph::Chain) chainIndex);
}

juce::String MainPluginProcessor::getPadPath (int padIndex) const
{
    return padBank.getPadPath (padIndex);
}

juce::StringArray MainPluginProcessor::getSupportedAudioExtensions() const
{
    return juce::StringArray::fromTokens (formatManager.getWildcardForAllFormats(), ";", "");
}

void MainPluginProcessor::restoreFileAssignmentsFromState()
{
    for (int i = 0; i < numDecks; ++i)
    {
        const auto path = apvts.state.getProperty (deckPathProperty (i)).toString();
        if (path.isNotEmpty())
            loadDeckFileAsync (i, juce::File (path));

        for (int lane = 1; lane < DeckPlayer::numStemLanes; ++lane)
        {
            const auto stemPath = apvts.state.getProperty (deckStemPathProperty (i, lane)).toString();
            if (stemPath.isNotEmpty())
                loadDeckStemFileAsync (i, lane, juce::File (stemPath));
        }
    }

    for (int i = 0; i < PadBank::numPads; ++i)
    {
        const auto path = apvts.state.getProperty (padPathProperty (i)).toString();
        if (path.isNotEmpty())
            loadPadFileAsync (i, juce::File (path));
    }
}

void MainPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("midiMap", midiMapping.serialize(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void MainPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        restoreFileAssignmentsFromState();
        midiMapping.deserialize (apvts.state.getProperty ("midiMap").toString());
    }
}

void MainPluginProcessor::logPerformanceEvent (int64_t sample, const juce::String& type, const juce::String& deck,
                                               const juce::String& parameter, const juce::String& value,
                                               const juce::String& path, const juce::String& details)
{
    performanceRecorder.logEvent (sample, type, deck, parameter, value, path, details);
}

void MainPluginProcessor::initialisePerformanceCaptureState()
{
    const int64_t sample = 0;
    lastCrossfader = getFloatParam ("crossfader");
    lastMasterBpm = getFloatParam ("masterBpm");
    lastMicGain = getFloatParam ("micGain");
    lastMicMonitor = getBoolParam ("micMonitor");
    lastHostSync = getBoolParam ("hostSync");
    lastBeatSync = getBoolParam ("beatSync");

    logPerformanceEvent (sample, "parameter_change", {}, "crossfader", juce::String (lastCrossfader, 4));
    logPerformanceEvent (sample, "parameter_change", {}, "master_bpm", juce::String (lastMasterBpm, 2));
    logPerformanceEvent (sample, lastMicMonitor ? "mic_start" : "mic_stop", {}, "mic_monitor", lastMicMonitor ? "on" : "off");
    logPerformanceEvent (sample, "sync_change", {}, "host_sync", lastHostSync ? "on" : "off");
    logPerformanceEvent (sample, "sync_change", {}, "beat_sync", lastBeatSync ? "on" : "off");

    for (int i = 0; i < numDecks; ++i)
    {
        const auto deck = deckPrefix (i);
        auto& state = lastDeckPerformanceState[(size_t) i];
        state.play = getBoolParam (deck + "Play");
        state.keyLock = getBoolParam (deck + "KeyLock");
        state.liveKey = getBoolParam (deck + "LiveKey");
        state.bpm = getFloatParam (deck + "Bpm");
        state.volume = getFloatParam (deck + "Volume");
        state.fader = getFloatParam (deck + "Fader");
        state.pitch = getFloatParam (deck + "Pitch");
        state.beatOffset = getFloatParam (deck + "BeatOffset");
        state.downbeat = getFloatParam (deck + "Downbeat");
        state.low = getFloatParam (deck + "Low");
        state.mid = getFloatParam (deck + "Mid");
        state.high = getFloatParam (deck + "High");
        state.filter = getFloatParam (deck + "Filter");
        state.tempoInterpretation = (int) std::round (getFloatParam (deck + "TempoInterpretation"));
        state.syncMode = (int) std::round (getFloatParam (deck + "SyncMode"));

        const auto deckName = deckNameFor (i);
        const auto path = getDeckPath (i);
        if (path.isNotEmpty())
            logPerformanceEvent (sample, "track_loaded", deckName, "full_mix", juce::File (path).getFileName(), path, "Initial deck source");
        logPerformanceEvent (sample, state.play ? "play_start" : "play_stop", deckName, "play", state.play ? "on" : "off", path);
        logPerformanceEvent (sample, "parameter_change", deckName, "bpm", juce::String (state.bpm, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "volume", juce::String (state.volume, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "fader", juce::String (state.fader, 4));
        logPerformanceEvent (sample, "parameter_change", deckName, "pitch", juce::String (state.pitch, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "eq_low", juce::String (state.low, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "eq_mid", juce::String (state.mid, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "eq_high", juce::String (state.high, 2));
        logPerformanceEvent (sample, "parameter_change", deckName, "filter", juce::String (state.filter, 4));
        logPerformanceEvent (sample, "sync_change", deckName, "sync_mode", juce::String (state.syncMode));
    }
}

void MainPluginProcessor::updatePerformanceCaptureState (int64_t blockStartSample, const TransportSync::Snapshot& snapshot)
{
    if (! performanceRecorder.isActive())
        return;

    auto changedFloat = [] (float a, float b, float threshold) { return std::abs (a - b) > threshold; };

    const float crossfaderValue = getFloatParam ("crossfader");
    if (changedFloat (crossfaderValue, lastCrossfader, 0.002f))
    {
        lastCrossfader = crossfaderValue;
        logPerformanceEvent (blockStartSample, "parameter_change", {}, "crossfader", juce::String (crossfaderValue, 4));
    }

    const float masterBpmValue = (float) snapshot.bpm;
    if (changedFloat (masterBpmValue, lastMasterBpm, 0.01f))
    {
        lastMasterBpm = masterBpmValue;
        logPerformanceEvent (blockStartSample, "parameter_change", {}, "master_bpm", juce::String (masterBpmValue, 2));
    }

    const float micGain = getFloatParam ("micGain");
    if (changedFloat (micGain, lastMicGain, 0.05f))
    {
        lastMicGain = micGain;
        logPerformanceEvent (blockStartSample, "parameter_change", {}, "mic_gain", juce::String (micGain, 2));
    }

    const bool micMonitor = getBoolParam ("micMonitor");
    if (micMonitor != lastMicMonitor)
    {
        lastMicMonitor = micMonitor;
        logPerformanceEvent (blockStartSample, micMonitor ? "mic_start" : "mic_stop", {}, "mic_monitor", micMonitor ? "on" : "off");
    }

    const bool hostSync = getBoolParam ("hostSync");
    if (hostSync != lastHostSync)
    {
        lastHostSync = hostSync;
        logPerformanceEvent (blockStartSample, "sync_change", {}, "host_sync", hostSync ? "on" : "off");
    }

    const bool beatSync = getBoolParam ("beatSync");
    if (beatSync != lastBeatSync)
    {
        lastBeatSync = beatSync;
        logPerformanceEvent (blockStartSample, "sync_change", {}, "beat_sync", beatSync ? "on" : "off");
    }

    for (int i = 0; i < numDecks; ++i)
    {
        const auto& ids = deckIds[(size_t) i];   // pre-concatenated ids — no RT alloc
        auto& state = lastDeckPerformanceState[(size_t) i];
        const auto deckName = deckNameFor (i);
        const auto path = getDeckPath (i);

        auto logFloat = [&] (juce::StringRef parameter, float value, float& previous, float threshold)
        {
            if (changedFloat (value, previous, threshold))
            {
                previous = value;
                logPerformanceEvent (blockStartSample, "parameter_change", deckName, parameter, juce::String (value, 4));
            }
        };

        const bool play = getBoolParam (ids.play);
        if (play != state.play)
        {
            state.play = play;
            logPerformanceEvent (blockStartSample, play ? "play_start" : "play_stop", deckName, "play", play ? "on" : "off", path);
        }

        logFloat ("bpm", getFloatParam (ids.bpm), state.bpm, 0.01f);
        logFloat ("volume", getFloatParam (ids.volume), state.volume, 0.05f);
        logFloat ("fader", getFloatParam (ids.fader), state.fader, 0.002f);
        logFloat ("pitch", getFloatParam (ids.pitch), state.pitch, 0.01f);
        logFloat ("beat_offset", getFloatParam (ids.beatOffset), state.beatOffset, 0.01f);
        logFloat ("downbeat", getFloatParam (ids.downbeat), state.downbeat, 0.01f);
        logFloat ("eq_low", getFloatParam (ids.low), state.low, 0.05f);
        logFloat ("eq_mid", getFloatParam (ids.mid), state.mid, 0.05f);
        logFloat ("eq_high", getFloatParam (ids.high), state.high, 0.05f);
        logFloat ("filter", getFloatParam (ids.filter), state.filter, 0.002f);

        const bool keyLock = getBoolParam (ids.keyLock);
        if (keyLock != state.keyLock)
        {
            state.keyLock = keyLock;
            logPerformanceEvent (blockStartSample, "effect_toggle", deckName, "key_lock", keyLock ? "on" : "off");
        }

        const bool liveKey = getBoolParam (ids.liveKey);
        if (liveKey != state.liveKey)
        {
            state.liveKey = liveKey;
            logPerformanceEvent (blockStartSample, "effect_toggle", deckName, "live_key", liveKey ? "on" : "off");
        }

        const int tempoInterpretation = (int) std::round (getFloatParam (ids.tempoInterp));
        if (tempoInterpretation != state.tempoInterpretation)
        {
            state.tempoInterpretation = tempoInterpretation;
            logPerformanceEvent (blockStartSample, "sync_change", deckName, "tempo_interpretation", juce::String (tempoInterpretation));
        }

        const int syncMode = (int) std::round (getFloatParam (ids.syncMode));
        if (syncMode != state.syncMode)
        {
            state.syncMode = syncMode;
            logPerformanceEvent (blockStartSample, "sync_change", deckName, "sync_mode", juce::String (syncMode));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MainPluginProcessor();
}
