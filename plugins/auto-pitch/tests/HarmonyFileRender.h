#pragma once
#include <limits>

// Local, explicit-input audition tool. This does not infer permission, key or a
// listening score. The operator supplies cleared audio and a NEW output path.
inline int renderHarmonyFile (const char* inputPath, const char* outputPath, bool live, int voices)
{
    const juce::File inputFile (juce::String::fromUTF8 (inputPath));
    const juce::File outputFile (juce::String::fromUTF8 (outputPath));
    if (outputFile.exists() || voices < 1 || voices > 4)
    { std::cerr << "Refusing existing output or invalid voice count\n"; return 2; }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inputFile));
    if (! reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > 600 * reader->sampleRate
        || reader->lengthInSamples > std::numeric_limits<int>::max() - 192000)
    { std::cerr << "Missing/invalid source or source exceeds ten-minute safety limit\n"; return 2; }
    const double sr = reader->sampleRate;
    const int length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> input (2, length);
    if (! reader->read (&input, 0, length, 0, true, true)) return 2;
    if (reader->numChannels == 1) input.copyFrom (1, 0, input, 0, 0, length);
    SattariAutoPitchAudioProcessor p;
    auto& a = p.parameters();
    for (const char* id : { "autoKey", "followSongKey", "neuralPitch", "polyphonic", "xposeMode",
                           "harmonyHumanPitch", "harmonyHumanTime", "harmonyEnsemble" })
        setParameter (a, id, 0);
    setParameter (a, "liveMode", live ? 1 : 0);
    setParameter (a, "extremeLive", 1);
    setParameter (a, "vocalRange", 0);
    setParameter (a, "scale", 2); setParameter (a, "scaleMode", 2);
    setParameter (a, "enableCorrection", 0); // isolate harmony, not lead tuning
    setParameter (a, "harmonyMode", 1);
    setParameter (a, "harmonyVoiceOnly", 1);
    setParameter (a, "harmonyMix", 1);
    setParameter (a, "inputGain", -12); // explicit fixed headroom, never normalization
    const int intervals[] { 4, -3, 7, -7 };
    for (int v = 0; v < 4; ++v)
    {
        const auto id = "h" + juce::String (v + 1);
        setParameter (a, id + "Enable", v < voices ? 1 : 0);
        setParameter (a, id + "Degrees", (float) intervals[v]);
        setParameter (a, id + "Level", -9);
        setParameter (a, id + "Detune", 0); setParameter (a, id + "Delay", 0);
        setParameter (a, id + "Formant", 0);
    }
    constexpr int blockSize = 256;
    p.prepareToPlay (sr, blockSize);
    const int latency = p.getLatencySamples();
    const int total = length + latency + (int) std::ceil (sr * 0.4);
    juce::AudioBuffer<float> output (2, total), block (2, blockSize);
    double peak = 0, energy = 0; int nonfinite = 0, clipped = 0;
    for (int offset = 0; offset < total; offset += blockSize)
    {
        const int n = std::min (blockSize, total - offset);
        block.clear();
        const int copied = std::max (0, std::min (n, length - offset));
        if (copied > 0)
            for (int ch = 0; ch < 2; ++ch) block.copyFrom (ch, 0, input, ch, offset, copied);
        juce::AudioBuffer<float> slice (block.getArrayOfWritePointers(), 2, n);
        juce::MidiBuffer midi; p.processBlock (slice, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            output.copyFrom (ch, offset, slice, ch, 0, n);
            for (int i = 0; i < n; ++i)
            {
                const float x = slice.getSample (ch, i);
                if (! std::isfinite (x)) { ++nonfinite; continue; }
                if (std::abs (x) >= 1) ++clipped;
                peak = std::max (peak, std::abs ((double) x)); energy += (double) x * x;
            }
        }
    }
    const double rms = std::sqrt (energy / (2.0 * total));
    std::cout << "harmony-file live=" << live << " voices=" << voices << " sr=" << sr
              << " block=" << blockSize << " latency=" << latency << " inputGainDb=-12 voiceGainDb=-9"
              << " peak=" << peak << " rms=" << rms << " nonfinite=" << nonfinite << " clipped=" << clipped << "\n";
    if (nonfinite || clipped || rms < 1.0e-8) return 1;
    return sattari::harness::writeWav (outputFile, output, sr) ? 0 : 2;
}
