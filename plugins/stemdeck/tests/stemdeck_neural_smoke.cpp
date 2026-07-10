// Smoke test for on-device neural separation through the app's real code path:
// NeuralStemEngine + ONNX Runtime 1.27 (+CoreML) on a real WAV. Loads the model
// from the default path, separates, prints per-stem RMS, writes the 4 stems.
//
//   StemDeckNeuralSmoke <input.wav> [outdir]
//
#include "NeuralStemEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <iostream>
#include <memory>

static double rms (const juce::AudioBuffer<float>& x)
{
    double s = 0.0; const int n = x.getNumSamples(), c = juce::jmax (1, x.getNumChannels());
    for (int ch = 0; ch < c; ++ch)
        for (int i = 0; i < n; ++i) { const double v = x.getSample (ch, i); s += v * v; }
    return std::sqrt (s / (double) (c * juce::jmax (1, n)));
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: StemDeckNeuralSmoke <input.wav> [outdir]\n"; return 1; }
    const juce::File input { juce::String (argv[1]) };
    const juce::File outdir = argc >= 3 ? juce::File { juce::String (argv[2]) } : input.getParentDirectory();

    std::cout << "compiledIn(ONNX): " << (NeuralStemEngine::isCompiledIn() ? "yes" : "no") << "\n";
    if (! NeuralStemEngine::isCompiledIn()) { std::cerr << "ONNX not compiled in\n"; return 2; }

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (input));
    if (rd == nullptr) { std::cerr << "cannot read " << input.getFullPathName() << "\n"; return 3; }

    const int len = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> raw ((int) rd->numChannels, len);
    rd->read (&raw, 0, len, 0, true, true);
    juce::AudioBuffer<float> mix (2, len);
    mix.copyFrom (0, 0, raw, 0, 0, len);
    mix.copyFrom (1, 0, raw, raw.getNumChannels() > 1 ? 1 : 0, 0, len);
    std::cout << "input: " << input.getFileName() << "  " << len << " samples ("
              << (double) len / rd->sampleRate << "s @ " << (int) rd->sampleRate << ")  mix rms=" << rms (mix) << "\n";

    NeuralStemEngine engine;
    juce::String msg;
    const auto modelFile = NeuralStemEngine::defaultModelFile();
    std::cout << "model: " << modelFile.getFullPathName() << " (" << modelFile.getSize() / (1024 * 1024) << " MB)\n";
    if (! engine.loadModel (modelFile, msg)) { std::cerr << "MODEL LOAD FAILED: " << msg << "\n"; return 4; }
    std::cout << "loaded: " << msg << "\n";

    juce::AudioBuffer<float> drums, bass, music, vocals;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = engine.separate (mix, rd->sampleRate, drums, bass, music, vocals,
                                     [] (float p) { std::cout << "\r  progress " << (int) (p * 100) << "%   " << std::flush; }, msg);
    std::cout << "\nseparate: " << (ok ? "OK" : "FAIL") << "  " << msg
              << "  (" << (int) (juce::Time::getMillisecondCounterHiRes() - t0) << " ms)\n";
    if (! ok) return 5;

    std::cout << "drums  rms=" << rms (drums)  << "\n"
              << "bass   rms=" << rms (bass)   << "\n"
              << "music  rms=" << rms (music)  << "\n"
              << "vocals rms=" << rms (vocals) << "\n";

    juce::WavAudioFormat wav;
    auto writeStem = [&] (juce::AudioBuffer<float>& b, const juce::String& name)
    {
        auto out = outdir.getChildFile ("stem_" + name + ".wav");
        out.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (
                    wav.createWriterFor (os.get(), rd->sampleRate, 2, 16, {}, 0)))
            { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
    };
    writeStem (drums, "drums"); writeStem (bass, "bass");
    writeStem (music, "music"); writeStem (vocals, "vocals");
    std::cout << "wrote 4 stems to " << outdir.getFullPathName() << "\nSMOKE_OK\n";
    return 0;
}
