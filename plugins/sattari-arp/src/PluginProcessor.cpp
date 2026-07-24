#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <sattari_audio_core/SattariHostTransport.h>
#include <sattari_audio_core/SattariPitchDetector.h>

namespace
{
constexpr int midiChannel = 1;

int clampMidiNote (int note)
{
    return juce::jlimit (0, 127, note);
}

int clampVelocity (int velocity)
{
    return juce::jlimit (1, 127, velocity);
}
}

SattariArpAudioProcessor::SattariArpAudioProcessor()
    : AudioProcessor (BusesProperties()),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    registerFactoryPresets();
}

// Factory presets: real parameter values. Only listed params are set; anything
// omitted keeps its default. Presets shape the arp pattern/feel; Scale Root,
// Scale and Follow Song Key are left untouched so the arp keeps the user's key.
void SattariArpAudioProcessor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;

    // rate:    0=1/4 1=1/8 2=1/8T 3=1/16 4=1/16T 5=1/32
    // pattern: 0=Up 1=Down 2=Up-Down 3=Down-Up 4=As Played 5=Random 6=Chord Pulse
    // velocityMode: 0=Input 1=Fixed 2=Accent   (swing range is 0..0.65)
    presetBank.addFactoryPreset ("Classic Up",
        PV{ {"rate", 3}, {"pattern", 0}, {"octaves", 2}, {"ratchet", 1}, {"gate", 0.7f}, {"swing", 0.0f}, {"velocityMode", 0} });
    presetBank.addFactoryPreset ("Staccato Gate",
        PV{ {"rate", 3}, {"pattern", 0}, {"octaves", 1}, {"ratchet", 1}, {"gate", 0.35f}, {"swing", 0.0f}, {"velocityMode", 0} });
    presetBank.addFactoryPreset ("Triplet Roll",
        PV{ {"rate", 2}, {"pattern", 2}, {"octaves", 2}, {"ratchet", 1}, {"gate", 0.6f}, {"swing", 0.0f}, {"velocityMode", 0} });
    presetBank.addFactoryPreset ("Swung Eighths",
        PV{ {"rate", 1}, {"pattern", 4}, {"octaves", 1}, {"ratchet", 1}, {"gate", 0.75f}, {"swing", 0.5f}, {"velocityMode", 0} });
    presetBank.addFactoryPreset ("Octave Cascade",
        PV{ {"rate", 3}, {"pattern", 1}, {"octaves", 4}, {"ratchet", 1}, {"gate", 0.85f}, {"swing", 0.0f}, {"velocityMode", 0} });
    presetBank.addFactoryPreset ("Ratchet Machine",
        PV{ {"rate", 3}, {"pattern", 0}, {"octaves", 2}, {"ratchet", 3}, {"gate", 0.5f}, {"swing", 0.0f}, {"velocityMode", 0} });
    presetBank.addFactoryPreset ("Random Motion",
        PV{ {"rate", 3}, {"pattern", 5}, {"octaves", 2}, {"ratchet", 1}, {"gate", 0.65f}, {"swing", 0.0f}, {"velocityMode", 2}, {"humanize", 8} });
    presetBank.addFactoryPreset ("Chord Pulse",
        PV{ {"rate", 1}, {"pattern", 6}, {"octaves", 1}, {"ratchet", 1}, {"gate", 0.9f}, {"swing", 0.0f}, {"velocityMode", 0} });
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariArpAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("rate", "Rate",
        juce::StringArray { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, 3));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("pattern", "Pattern",
        juce::StringArray { "Up", "Down", "Up-Down", "Down-Up", "As Played", "Random", "Chord Pulse" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("octaves", "Octaves", 1, 4, 1));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("ratchet", "Ratchet", 1, 4, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scaleRoot", "Scale Root",
        juce::StringArray { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 0));
    // Cross-plugin session bus: when on, the arp's scale root follows the song key
    // published by Sattari Key (or any Sattari publisher). Default OFF so the manual
    // Root menu is authoritative out of the box; the editor greys Root while Follow
    // is engaged so the override is never silent.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("followKey", "Follow Song Key", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale",
        juce::StringArray { "Off", "Major", "Minor", "Penta Maj", "Penta Min", "Dorian", "Mixolydian", "Harm Minor" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gate", "Gate",
        juce::NormalisableRange<float> (0.10f, 1.00f, 0.01f), 0.72f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("swing", "Swing",
        juce::NormalisableRange<float> (0.0f, 0.65f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("velocityMode", "Velocity",
        juce::StringArray { "Input", "Fixed", "Accent" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("fixedVelocity", "Fixed Velocity", 1, 127, 96));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("humanize", "Humanize", 0, 20, 0));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("transpose", "Transpose", -24, 24, 0));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("latch", "Latch", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("inputThru", "Input Thru", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("hostSync", "Host Sync", true));

    // Cross-plugin session follows (Brain bus), appended at the END of the layout
    // so existing parameter indices/automation stay stable. Both default OFF so
    // the arp is bit-identical to previous versions until the user opts in.
    //  * followSwing: the published session swing (0..1) is applied as classic
    //    off-beat delay scaled to the arp's division — every 2nd step lands late
    //    by up to ~2/3 of a step at swing = 1.
    //  * followEnergy: the published arrangement energy (0..1) drives dynamics —
    //    velocity scales by (0.5 + 0.5*energy), and above 0.75 the pattern range
    //    gains +1 octave and the hits get an accent lift (audible chorus/drop).
    params.push_back (std::make_unique<juce::AudioParameterBool> ("followSwing", "Follow Swing", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("followEnergy", "Follow Energy", false));

    return { params.begin(), params.end() };
}

void SattariArpAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    refreshHostTempo();
    resetSequence();
    sessionReader.start();   // poll the cross-plugin session bus off the audio thread
    tempoReader.start();     // poll the tempo bus (fallback when the host has no tempo)
    outputNotes.clear();
    heldNotes.clear();
    wasPlayingNotes = false;
    hostWasPlaying = false;
    lastPpq = -1.0;

    // Preallocate realtime scratch so processBlock/triggerStep never allocate.
    constexpr int reserveNotes = 512;
    heldNotes.reserve (reserveNotes);
    outputNotes.reserve (reserveNotes);
    baseNotesScratch.reserve (reserveNotes);
    stepNotesScratch.reserve (reserveNotes);
    currentStepNotes.reserve (reserveNotes);
    scratchOutput.ensureSize (4096);
}

bool SattariArpAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    juce::ignoreUnused (layouts);
    return true;
}

void SattariArpAudioProcessor::refreshHostTempo()
{
    const auto t = sattari::host::read (*this);
    if (t.hasHostBpm)
        currentBpm = t.bpm;
    else if (auto s = tempoReader.current(); s.valid && s.bpm > 1.0)   // tempo-bus fallback
        currentBpm = s.bpm;
}

void SattariArpAudioProcessor::resetSequence()
{
    samplesUntilNextStep = 0;
    samplesUntilGateOff = -1;
    stepCounter = 0;
    sequenceCursor = 0;
    ratchetIndex = 0;
    ratchetCountForStep = 1;
    currentStepNotes.clear();
}

// Keep the arp well-behaved against the host transport:
//   * stop while playing  -> note-off any sounding notes (no hangs) + restart grid
//   * start playing       -> restart the sequence so it locks to the downbeat
//   * loop wrap (ppq jumps back) -> restart the sequence at the loop point
// The arp still free-runs while the transport is stopped (live chord hold).
void SattariArpAudioProcessor::handleTransport (juce::MidiBuffer& output)
{
    const bool syncEnabled = *apvts.getRawParameterValue ("hostSync") > 0.5f;

    const auto t = sattari::host::read (*this);
    const bool isPlaying = t.isPlaying;
    const double ppq = t.ppqPosition;

    if (hostWasPlaying && ! isPlaying)
    {
        // Transport just stopped: clear any sounding notes so nothing hangs.
        stopOutputNotes (output, 0);
        resetSequence();
    }
    else if (syncEnabled && isPlaying)
    {
        const bool justStarted = ! hostWasPlaying;
        const bool loopedBack  = lastPpq >= 0.0 && ppq >= 0.0 && ppq < lastPpq - 1.0e-6;
        if (justStarted || loopedBack)
        {
            stopOutputNotes (output, 0);
            resetSequence();
        }
    }

    hostWasPlaying = isPlaying;
    lastPpq = ppq;
}

// Snap a MIDI note to the selected key/scale via the shared suite helper. The
// "scale" parameter is laid out to match sattari::dsp::ScaleId (Off=0..HarmMinor=7),
// so the index can be cast straight through.
int SattariArpAudioProcessor::snapNoteToScale (int note) const
{
    const int scaleIndex = (int) *apvts.getRawParameterValue ("scale");
    int root = (int) *apvts.getRawParameterValue ("scaleRoot");
    int scale = scaleIndex;
    if (apvts.getRawParameterValue ("followKey")->load() > 0.5f)
        if (auto s = sessionReader.current(); s.valid)
        {
            root = s.root;
            scale = s.scale == 0 ? 1 : (s.scale == 1 ? 2 : 0);
        }
    return sattari::dsp::snapMidiToScale (note, root, (sattari::dsp::ScaleId) scale);
}

SattariArpAudioProcessor::KeyState SattariArpAudioProcessor::currentKeyState() const
{
    KeyState k;
    k.scale = (int) *apvts.getRawParameterValue ("scale");
    k.root  = (int) *apvts.getRawParameterValue ("scaleRoot");
    k.following = apvts.getRawParameterValue ("followKey")->load() > 0.5f;
    if (k.following)
        if (auto s = sessionReader.current(); s.valid)
        {
            k.root = s.root;
            k.scale = s.scale == 0 ? 1 : (s.scale == 1 ? 2 : 0);
        }
    return k;
}

double SattariArpAudioProcessor::getStepQuarterNotes() const
{
    switch ((int) *apvts.getRawParameterValue ("rate"))
    {
        case 0: return 1.0;        // 1/4
        case 1: return 0.5;        // 1/8
        case 2: return 1.0 / 3.0;  // 1/8 triplet
        case 3: return 0.25;       // 1/16
        case 4: return 1.0 / 6.0;  // 1/16 triplet
        case 5: return 0.125;      // 1/32
        default: return 0.25;
    }
}

int SattariArpAudioProcessor::getNextStepSamples()
{
    const double beatsPerSecond = currentBpm / 60.0;
    const double seconds = getStepQuarterNotes() / beatsPerSecond;
    const float swing = *apvts.getRawParameterValue ("swing");
    const bool tripletRate = (int) *apvts.getRawParameterValue ("rate") == 2 || (int) *apvts.getRawParameterValue ("rate") == 4;
    const double swingFactor = (! tripletRate && swing > 0.0f) ? ((stepCounter % 2 == 0) ? (1.0 + swing) : (1.0 - swing)) : 1.0;
    return juce::jmax (1, (int) std::llround (seconds * swingFactor * currentSampleRate));
}

void SattariArpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midiLearn.processMidi (midi);   // read-only: notes still flow into the arp
    refreshHostTempo();

    // Reuse the member MidiBuffer instead of allocating a fresh one each block.
    scratchOutput.clear();
    handleTransport (scratchOutput);
    int cursor = 0;

    for (const auto metadata : midi)
    {
        const int eventSample = juce::jlimit (0, buffer.getNumSamples(), metadata.samplePosition);
        if (eventSample > cursor)
        {
            renderArp (scratchOutput, cursor, eventSample - cursor);
            cursor = eventSample;
        }

        handleInputMidi (metadata.getMessage(), eventSample, scratchOutput);
    }

    if (cursor < buffer.getNumSamples())
        renderArp (scratchOutput, cursor, buffer.getNumSamples() - cursor);

    midi.swapWith (scratchOutput);
}

void SattariArpAudioProcessor::handleInputMidi (const juce::MidiMessage& message, int samplePosition, juce::MidiBuffer& output)
{
    const bool inputThru = *apvts.getRawParameterValue ("inputThru") > 0.5f;
    const bool hadNotes = hasHeldNotes();

    if (message.isNoteOn())
    {
        noteOnInput (message.getNoteNumber(), message.getVelocity());
        if (inputThru)
            output.addEvent (message, samplePosition);
    }
    else if (message.isNoteOff())
    {
        noteOffInput (message.getNoteNumber());
        if (inputThru)
            output.addEvent (message, samplePosition);
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        clearHeldNotes();
        stopOutputNotes (output, samplePosition);
        output.addEvent (message, samplePosition);
    }
    else
    {
        output.addEvent (message, samplePosition);
    }

    const bool hasNotesNow = hasHeldNotes();
    if (! hadNotes && hasNotesNow)
    {
        resetSequence();   // first note held: trigger immediately, fresh sequence
    }
    else if (hadNotes && ! hasNotesNow)
    {
        stopOutputNotes (output, samplePosition);
        resetSequence();
    }
}

void SattariArpAudioProcessor::noteOnInput (int note, int velocity)
{
    const bool latch = *apvts.getRawParameterValue ("latch") > 0.5f;
    const auto it = std::find_if (heldNotes.begin(), heldNotes.end(), [note] (const HeldNote& n) { return n.note == note; });

    if (latch && it != heldNotes.end())
    {
        heldNotes.erase (it);
        return;
    }

    if (it != heldNotes.end())
    {
        it->velocity = clampVelocity (velocity);
        return;
    }

    heldNotes.push_back ({ clampMidiNote (note), clampVelocity (velocity), ++inputOrderCounter });
}

void SattariArpAudioProcessor::noteOffInput (int note)
{
    const bool latch = *apvts.getRawParameterValue ("latch") > 0.5f;
    if (latch)
        return;

    heldNotes.erase (std::remove_if (heldNotes.begin(), heldNotes.end(), [note] (const HeldNote& n) { return n.note == note; }), heldNotes.end());
}

void SattariArpAudioProcessor::clearHeldNotes()
{
    heldNotes.clear();
}

void SattariArpAudioProcessor::stopOutputNotes (juce::MidiBuffer& output, int samplePosition)
{
    for (int note : outputNotes)
        output.addEvent (juce::MidiMessage::noteOff (midiChannel, note), samplePosition);

    outputNotes.clear();
}

void SattariArpAudioProcessor::renderArp (juce::MidiBuffer& output, int startSample, int numSamples)
{
    int cursor = 0;

    while (cursor < numSamples)
    {
        int nextEvent = numSamples - cursor;

        if (! outputNotes.empty() && samplesUntilGateOff >= 0)
            nextEvent = juce::jmin (nextEvent, samplesUntilGateOff);

        nextEvent = juce::jmin (nextEvent, samplesUntilNextStep);

        if (nextEvent > 0)
        {
            cursor += nextEvent;
            samplesUntilNextStep -= nextEvent;
            if (samplesUntilGateOff >= 0)
                samplesUntilGateOff -= nextEvent;
            continue;
        }

        const int absoluteSample = startSample + cursor;

        if (! outputNotes.empty() && samplesUntilGateOff == 0)
        {
            stopOutputNotes (output, absoluteSample);
            samplesUntilGateOff = -1;
            continue;
        }

        if (samplesUntilNextStep <= 0)
        {
            triggerStep (output, absoluteSample);
            continue;
        }

        ++cursor;
    }
}

void SattariArpAudioProcessor::buildSortedBaseNotes (std::vector<int>& out) const
{
    // heldNotes is maintained in as-played insertion order (push_back on note-on,
    // order-preserving erase on note-off), so "As Played" needs no extra sort/copy.
    out.clear();
    for (const auto& note : heldNotes)
        out.push_back (note.note);

    const int pattern = (int) *apvts.getRawParameterValue ("pattern");
    if (pattern != 4) // anything but "As Played" wants ascending pitch order
        std::sort (out.begin(), out.end());
}

void SattariArpAudioProcessor::buildStepNotes (std::vector<int>& out) const
{
    buildSortedBaseNotes (baseNotesScratch);
    const int octaves = (int) *apvts.getRawParameterValue ("octaves");
    const int transpose = (int) *apvts.getRawParameterValue ("transpose");
    out.clear();

    for (int octave = 0; octave < octaves; ++octave)
        for (int note : baseNotesScratch)
        {
            const int shifted = snapNoteToScale (note + octave * 12 + transpose);
            if (juce::isPositiveAndBelow (shifted, 128))
                out.push_back (shifted);
        }
}

const std::vector<int>& SattariArpAudioProcessor::chooseNotesForStep()
{
    buildStepNotes (stepNotesScratch);
    if (stepNotesScratch.empty())
        return stepNotesScratch;

    const int pattern = (int) *apvts.getRawParameterValue ("pattern");

    if (pattern == 6) // Chord Pulse -> sound all notes together
        return stepNotesScratch;

    if (pattern == 1 || pattern == 3) // Down / Down-Up
        std::reverse (stepNotesScratch.begin(), stepNotesScratch.end());

    if ((pattern == 2 || pattern == 3) && stepNotesScratch.size() > 1) // Up-Down / Down-Up
    {
        const int originalSize = (int) stepNotesScratch.size();
        for (int i = originalSize - 2; i > 0; --i)
            stepNotesScratch.push_back (stepNotesScratch[(size_t) i]);
    }

    int chosen;
    if (pattern == 5) // Random
    {
        chosen = stepNotesScratch[(size_t) random.nextInt ((int) stepNotesScratch.size())];
    }
    else
    {
        const int index = sequenceCursor % (int) stepNotesScratch.size();
        ++sequenceCursor;
        chosen = stepNotesScratch[(size_t) index];
    }

    stepNotesScratch.assign (1, chosen); // single-note step; assign keeps capacity (no realloc)
    return stepNotesScratch;
}

int SattariArpAudioProcessor::chooseVelocity (int sourceVelocity)
{
    const int mode = (int) *apvts.getRawParameterValue ("velocityMode");
    const int fixed = (int) *apvts.getRawParameterValue ("fixedVelocity");
    const int humanize = (int) *apvts.getRawParameterValue ("humanize");

    int velocity = sourceVelocity;
    if (mode == 1)
        velocity = fixed;
    else if (mode == 2)
        velocity = (stepCounter % 4 == 0) ? 118 : 88;

    if (humanize > 0)
        velocity += random.nextInt (humanize * 2 + 1) - humanize;

    return clampVelocity (velocity);
}

void SattariArpAudioProcessor::triggerStep (juce::MidiBuffer& output, int samplePosition)
{
    stopOutputNotes (output, samplePosition);

    // Pick notes + velocity once per *musical* step; ratchet repeats them.
    if (ratchetIndex == 0)
    {
        const auto& notes = chooseNotesForStep();
        currentStepNotes.assign (notes.begin(), notes.end());
        ratchetCountForStep = juce::jlimit (1, 4, (int) *apvts.getRawParameterValue ("ratchet"));

        int sourceVelocity = 96;
        if (! heldNotes.empty())
        {
            const auto velocityIndex = ((size_t) sequenceCursor + heldNotes.size() - 1) % heldNotes.size();
            sourceVelocity = heldNotes[velocityIndex].velocity;
        }
        currentStepVelocity = chooseVelocity (sourceVelocity);
    }

    const int stepSamples = getNextStepSamples();
    const int subStep     = juce::jmax (1, stepSamples / ratchetCountForStep);
    samplesUntilNextStep  = subStep;

    auto advanceMusicalStep = [this]
    {
        if (++ratchetIndex >= ratchetCountForStep)
        {
            ratchetIndex = 0;
            ++stepCounter;
        }
    };

    if (currentStepNotes.empty())
    {
        samplesUntilGateOff = -1;
        advanceMusicalStep();
        return;
    }

    for (int note : currentStepNotes)
    {
        outputNotes.push_back (note);
        output.addEvent (juce::MidiMessage::noteOn (midiChannel, note, (juce::uint8) currentStepVelocity), samplePosition);
    }

    const float gate = *apvts.getRawParameterValue ("gate");
    samplesUntilGateOff = juce::jmax (1, (int) std::floor ((float) subStep * gate));
    advanceMusicalStep();
}

juce::AudioProcessorEditor* SattariArpAudioProcessor::createEditor()
{
    return new SattariArpAudioProcessorEditor (*this);
}

void SattariArpAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SattariArpAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        midiLearn.loadFrom (state);
        apvts.replaceState (state);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariArpAudioProcessor();
}
