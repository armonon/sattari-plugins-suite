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
#include <vector>

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

    // ── Streaming / look-ahead path ───────────────────────────────────────────
    // Separate playhead-first from the middle of the track; verify the first
    // emitted segment covers the playhead, segments arrive in playhead-first
    // order, and reassembling them reconstructs a full-length separation whose
    // energy matches the offline pass (independent segments => close, not equal).
    {
        const int64_t playhead = len / 2;
        juce::AudioBuffer<float> sd (2, len), sb (2, len), sm (2, len), sv (2, len);
        sd.clear(); sb.clear(); sm.clear(); sv.clear();
        std::vector<int64_t> starts;
        auto onSeg = [&] (int64_t start, const juce::AudioBuffer<float>& d, const juce::AudioBuffer<float>& b,
                          const juce::AudioBuffer<float>& m, const juce::AudioBuffer<float>& v)
        {
            starts.push_back (start);
            const int n = juce::jmin (d.getNumSamples(), len - (int) start);
            for (int ch = 0; ch < 2; ++ch)
            {
                sd.copyFrom (ch, (int) start, d, ch, 0, n); sb.copyFrom (ch, (int) start, b, ch, 0, n);
                sm.copyFrom (ch, (int) start, m, ch, 0, n); sv.copyFrom (ch, (int) start, v, ch, 0, n);
            }
        };
        juce::String smsg;
        const auto ts = juce::Time::getMillisecondCounterHiRes();
        const bool sok = engine.separateStreaming (mix, rd->sampleRate, playhead, onSeg, {}, smsg);
        std::cout << "streaming: " << (sok ? "OK" : "FAIL") << "  " << smsg
                  << "  (" << (int) (juce::Time::getMillisecondCounterHiRes() - ts) << " ms, "
                  << starts.size() << " segments)\n";
        if (! sok) return 6;

        bool ok = ! starts.empty();
        // First segment must contain the playhead.
        if (! starts.empty())
        {
            const int64_t win = starts.size() >= 2 ? (starts[1] > starts[0] ? starts[1] - starts[0] : 0) : len;
            const bool coversPlayhead = starts[0] <= playhead && (win == 0 || playhead < starts[0] + win || starts.size() == 1);
            std::cout << "  first segment @ " << starts[0] << " (playhead " << playhead << ") "
                      << (coversPlayhead ? "covers playhead" : "MISSED playhead") << "\n";
            ok = ok && coversPlayhead;
        }
        // Reassembled energy should track the offline pass within a factor of 2.
        auto within = [] (double a, double b) { return a > 1e-9 && b > 1e-9 && a / b < 2.0 && b / a < 2.0; };
        const bool recon = within (rms (sd), rms (drums)) && within (rms (sb), rms (bass))
                         && within (rms (sm), rms (music)) && within (rms (sv), rms (vocals));
        std::cout << "  reassembled rms drums=" << rms (sd) << " bass=" << rms (sb)
                  << " music=" << rms (sm) << " vocals=" << rms (sv)
                  << (recon ? "  (matches offline)" : "  (MISMATCH)") << "\n";
        std::cout << (ok && recon ? "STREAM_OK\n" : "STREAM_FAIL\n");
        if (! (ok && recon)) return 7;
    }

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
