#include "PluginProcessor.h"
#include "PluginEditor.h"

SattariVoxKeyAudioProcessor::SattariVoxKeyAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Voice In", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Dry Voice Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    apvts.state.setProperty ("schema", "SattariVoxKey.v0.1.0", nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout SattariVoxKeyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> ("mode", "Mode",
        juce::StringArray { "Melody", "Bass Follow", "Harmony", "Drum Mouth", "Pad/Soul Layer" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("key", "Key",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    // Cross-plugin session bus: when on, the key follows the song key published
    // by Auto Pitch (or any Sattari publisher), so voice->MIDI stays in key.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("followKey", "Follow Song Key", true));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("scale", "Scale",
        juce::StringArray { "Chromatic", "Major", "Minor", "Pentatonic" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("sensitivity", "Sensitivity", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.62f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("smoothness", "Smoothness", juce::NormalisableRange<float> (0.0f, 0.98f, 0.01f), 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("confidence", "Confidence", juce::NormalisableRange<float> (0.20f, 0.95f, 0.01f), 0.58f));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("octave", "Octave", -3, 3, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("hold", "Hold", juce::NormalisableRange<float> (10.0f, 300.0f, 1.0f), 60.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("release", "Release", juce::NormalisableRange<float> (20.0f, 1000.0f, 1.0f), 140.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("velocity", "Velocity Response", juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.72f));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("pitchBend", "Pitch Bend", false));
    params.push_back (std::make_unique<juce::AudioParameterInt> ("pitchBendRange", "Bend Range", 1, 12, 2));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("glide", "Glide", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> ("bypass", "Bypass", false));
    // Paraphonic mode (alpha): use a Harmonic Product Spectrum peak-picker on
    // the FFT magnitude of the input frame to extract up to 3 simultaneous
    // notes, instead of YIN single-pitch tracking. NOT polyphonic in the
    // CREPE / Melodyne sense - this works on simple chord material (piano
    // dyads, mono-synth chords) but loses notes that are masked by louder
    // harmonics. UI marks it 'alpha'.
    params.push_back (std::make_unique<juce::AudioParameterBool> ("paraphonic", "Paraphonic (alpha)", false));

    return { params.begin(), params.end() };
}

void SattariVoxKeyAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitchDetector.prepare (sampleRate, samplesPerBlock);
    midiMapper.prepare (sampleRate);
    monoScratch.setSize (1, juce::jmax (1, samplesPerBlock), false, false, true);
    sessionReader.start();   // poll the cross-plugin session bus off the audio thread
    publishDisplayState ({});

    // Paraphonic detector init (allocation-free after this).
    paraFft = std::make_unique<juce::dsp::FFT> (paraFftOrder);
    paraRing.assign ((size_t) paraFftSize, 0.0f);
    paraFftBuf.assign ((size_t) (paraFftSize * 2), 0.0f);
    paraHann.assign ((size_t) paraFftSize, 0.0f);
    for (int i = 0; i < paraFftSize; ++i)
        paraHann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * (float) i / (float) (paraFftSize - 1));
    paraWritePos = 0;
    paraSamplesSinceFft = 0;
    paraCurrentNotes.fill (-1);
    paraActiveNotes.fill (-1);
}

std::array<int, 3> SattariVoxKeyAudioProcessor::paraDetectFrame (double sr)
{
    // Hann-window + FFT the ring.
    for (int i = 0; i < paraFftSize; ++i)
        paraFftBuf[(size_t) i] = paraRing[(size_t) ((paraWritePos + i) % paraFftSize)]
                                  * paraHann[(size_t) i];
    std::fill (paraFftBuf.begin() + paraFftSize, paraFftBuf.end(), 0.0f);
    paraFft->performFrequencyOnlyForwardTransform (paraFftBuf.data());

    // Harmonic Product Spectrum: multiply 5 downsampled spectra. Limit to the
    // useful band [60..1500 Hz] for vocal-instrument chords.
    const double binHz = sr / (double) paraFftSize;
    const int firstBin = juce::jmax (2, (int) std::ceil (60.0  / binHz));
    const int lastBin  = juce::jmin (paraFftSize / 10, (int) std::floor (1500.0 / binHz));
    std::vector<float> hps ((size_t) (lastBin + 1), 0.0f);
    for (int k = firstBin; k <= lastBin; ++k)
    {
        float prod = paraFftBuf[(size_t) k];
        for (int h = 2; h <= 5; ++h)
            prod *= paraFftBuf[(size_t) (k * h)];
        hps[(size_t) k] = prod;
    }

    // Pick the 3 highest local peaks (max within a 3-bin neighbourhood) above
    // 30% of the global max. Returns -1 for empty slots.
    std::array<int, 3> result { -1, -1, -1 };
    float globalMax = 0.0f;
    for (int k = firstBin; k <= lastBin; ++k) globalMax = juce::jmax (globalMax, hps[(size_t) k]);
    if (globalMax < 1.0e-6f) return result;
    const float threshold = globalMax * 0.30f;

    struct Peak { float val; int bin; };
    std::vector<Peak> peaks;
    for (int k = firstBin + 1; k < lastBin; ++k)
    {
        if (hps[(size_t) k] >= threshold
            && hps[(size_t) k] >= hps[(size_t) (k - 1)]
            && hps[(size_t) k] >= hps[(size_t) (k + 1)])
            peaks.push_back ({ hps[(size_t) k], k });
    }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.val > b.val; });

    int outIdx = 0;
    for (const auto& p : peaks)
    {
        const double f    = (double) p.bin * binHz;
        const int    midi = juce::jlimit (0, 127, (int) std::round (69.0 + 12.0 * std::log2 (f / 440.0)));
        bool tooClose = false;
        for (int j = 0; j < outIdx; ++j)
            if (std::abs (result[(size_t) j] - midi) < 2) { tooClose = true; break; }   // dedupe near-doubles
        if (! tooClose) { result[(size_t) outIdx++] = midi; if (outIdx == 3) break; }
    }
    return result;
}

void SattariVoxKeyAudioProcessor::paraProcessBlock (const float* mono, int n, double sr, juce::MidiBuffer& midi)
{
    // Push into the ring.
    for (int i = 0; i < n; ++i)
    {
        paraRing[(size_t) paraWritePos] = mono[i];
        paraWritePos = (paraWritePos + 1) % paraFftSize;
    }
    paraSamplesSinceFft += n;

    // Re-detect every ~50 ms; in between, the most recent notes are held.
    const int hop = (int) (sr * 0.05);
    if (paraSamplesSinceFft < hop) return;
    paraSamplesSinceFft = 0;

    const auto detected = paraDetectFrame (sr);

    // Diff active vs detected: turn off notes that left, turn on new ones.
    constexpr int midiChannel = 1;
    auto inSet = [] (const std::array<int, 3>& set, int n) {
        for (int v : set) if (v == n) return true;
        return false;
    };
    for (int i = 0; i < 3; ++i)
    {
        const int a = paraActiveNotes[(size_t) i];
        if (a >= 0 && ! inSet (detected, a))
        {
            midi.addEvent (juce::MidiMessage::noteOff (midiChannel, a), 0);
            paraActiveNotes[(size_t) i] = -1;
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        const int d = detected[(size_t) i];
        if (d >= 0 && ! inSet (paraActiveNotes, d))
        {
            // Find a free slot.
            for (int j = 0; j < 3; ++j)
                if (paraActiveNotes[(size_t) j] < 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (midiChannel, d, (juce::uint8) 96), 0);
                    paraActiveNotes[(size_t) j] = d;
                    break;
                }
        }
    }
    paraCurrentNotes = detected;
}

bool SattariVoxKeyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == out) && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

VoxKeyVoiceMidiMapper::Settings SattariVoxKeyAudioProcessor::readSettings() const
{
    VoxKeyVoiceMidiMapper::Settings s;
    s.mode = (VoxKeyVoiceMidiMapper::Mode) (int) *apvts.getRawParameterValue ("mode");
    s.keyRoot = (int) *apvts.getRawParameterValue ("key");
    if (apvts.getRawParameterValue ("followKey")->load() > 0.5f)
        if (auto sess = sessionReader.current(); sess.valid)
            s.keyRoot = sess.root;
    s.scale = (VoxKeyVoiceMidiMapper::Scale) (int) *apvts.getRawParameterValue ("scale");
    s.octaveShift = (int) *apvts.getRawParameterValue ("octave");
    s.holdMs = *apvts.getRawParameterValue ("hold");
    s.releaseMs = *apvts.getRawParameterValue ("release");
    s.velocityResponse = *apvts.getRawParameterValue ("velocity");
    s.noteStabilityCents = 45.0f;
    s.pitchBendRangeSemis = *apvts.getRawParameterValue ("pitchBendRange");
    // Onset gate sits a margin above the detector's own confidence floor so
    // breaths/consonants don't spawn notes, while sustained pitch changes pass.
    s.startConfidence = juce::jlimit (0.0f, 1.0f, *apvts.getRawParameterValue ("confidence") + 0.12f);
    s.pitchBend = *apvts.getRawParameterValue ("pitchBend") > 0.5f;
    s.glide = *apvts.getRawParameterValue ("glide") > 0.5f;
    s.bypass = *apvts.getRawParameterValue ("bypass") > 0.5f;
    return s;
}

void SattariVoxKeyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midiLearn.processMidi (midi);

    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    for (int ch = numInputChannels; ch < numOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    midi.clear();

    if (panicRequested.exchange (false, std::memory_order_relaxed))
    {
        midiMapper.reset (midi, 0);
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    }

    if (monoScratch.getNumSamples() < numSamples)
        monoScratch.setSize (1, numSamples, false, false, true);

    auto* mono = monoScratch.getWritePointer (0);
    std::fill (mono, mono + numSamples, 0.0f);
    const int channelsToRead = juce::jmax (1, numInputChannels);
    for (int ch = 0; ch < channelsToRead; ++ch)
    {
        const float* src = buffer.getReadPointer (juce::jmin (ch, buffer.getNumChannels() - 1));
        for (int i = 0; i < numSamples; ++i)
            mono[i] += src[i] / (float) channelsToRead;
    }

    const bool paraOn = apvts.getRawParameterValue ("paraphonic")->load() > 0.5f;
    if (paraOn)
    {
        // Paraphonic alpha path. Bypass YIN + midiMapper; emit notes directly.
        // Still update the UI display from the first detected note so the orb
        // and detected/output labels stay alive.
        paraProcessBlock (mono, numSamples, getSampleRate(), midi);
        VoxKeyVoiceMidiMapper::State display;
        display.voiced = paraActiveNotes[0] >= 0;
        display.detectedNote = paraActiveNotes[0];
        display.outputRoot = paraActiveNotes[0];
        display.detectedName = paraActiveNotes[0] >= 0
            ? VoxKeyVoiceMidiMapper::midiNoteName (paraActiveNotes[0]) : juce::String ("--");
        display.outputName = display.detectedName;
        display.midiActive = paraActiveNotes[0] >= 0
                          || paraActiveNotes[1] >= 0
                          || paraActiveNotes[2] >= 0;
        publishDisplayState (display);
        return;
    }

    const auto frame = pitchDetector.analyseBlock (mono,
                                                   numSamples,
                                                   *apvts.getRawParameterValue ("sensitivity"),
                                                   *apvts.getRawParameterValue ("confidence"),
                                                   *apvts.getRawParameterValue ("smoothness"));

    midiMapper.processFrame (frame, readSettings(), midi, 0);
    publishDisplayState (midiMapper.getState());
}

void SattariVoxKeyAudioProcessor::publishDisplayState (const VoxKeyVoiceMidiMapper::State& state)
{
    displayRms.store (state.inputRms, std::memory_order_relaxed);
    displayConfidence.store (state.confidence, std::memory_order_relaxed);
    displayDetectedNote.store (state.detectedNote, std::memory_order_relaxed);
    displayOutputNote.store (state.outputRoot, std::memory_order_relaxed);
    displayVoiced.store (state.voiced, std::memory_order_relaxed);
    displayMidiActive.store (state.midiActive, std::memory_order_relaxed);
}

VoxKeyVoiceMidiMapper::State SattariVoxKeyAudioProcessor::getDisplayState() const
{
    VoxKeyVoiceMidiMapper::State state;
    state.inputRms = displayRms.load (std::memory_order_relaxed);
    state.confidence = displayConfidence.load (std::memory_order_relaxed);
    state.detectedNote = displayDetectedNote.load (std::memory_order_relaxed);
    state.outputRoot = displayOutputNote.load (std::memory_order_relaxed);
    state.detectedName = VoxKeyVoiceMidiMapper::midiNoteName (state.detectedNote);
    state.outputName = VoxKeyVoiceMidiMapper::midiNoteName (state.outputRoot);
    state.voiced = displayVoiced.load (std::memory_order_relaxed);
    state.midiActive = displayMidiActive.load (std::memory_order_relaxed);
    return state;
}

void SattariVoxKeyAudioProcessor::panic()
{
    panicRequested.store (true, std::memory_order_relaxed);
}

void SattariVoxKeyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    midiLearn.saveTo (state);
    juce::MemoryOutputStream stream (destData, true);
    state.writeToStream (stream);
}

void SattariVoxKeyAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid())
    {
        midiLearn.loadFrom (tree);
        apvts.replaceState (tree);
    }
}

juce::AudioProcessorEditor* SattariVoxKeyAudioProcessor::createEditor()
{
    return new SattariVoxKeyAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SattariVoxKeyAudioProcessor();
}
