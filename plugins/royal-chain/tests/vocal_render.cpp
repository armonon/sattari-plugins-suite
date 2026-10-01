// Royal Chain — real-vocal render + measurement tool (local evidence, not a CI gate).
//
// Streams a vocal recording through the REAL RoyalChainAudioProcessor with its
// editor open, then reports:
//   * pitch correction on actual singing: per voiced frame, |cents from the
//     nearest semitone| of the input vs the latency-aligned output (Chromatic
//     scale, so no key assumption), median / p90 / share within +-10 c;
//   * output sanity: finite, peak, RMS, reported latency;
//   * processing speed (x realtime) and processBlock allocation count;
//   * Compass water-dish state at chosen moments, with PNG snapshots.
// Writes input/output WAVs for listening. Audio never goes into the repo.
//
//   RoyalChainVocalRender --in <wav> --out <dir> [--start <s>] [--seconds <s>]
//                         [--tune <0..1>] [--style <0..9>] [--shots <n>]
#include "RoyalChainProcessor.h"
#include "RoyalChainEditor.h"
#include <sattari_audio_core/SattariPitchDetector.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

namespace
{
thread_local bool gCountAllocs = false;   // per thread: only the audio call is counted
std::atomic<long> gAllocs { 0 };
}

void* operator new (std::size_t n)
{
    if (gCountAllocs) gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (gCountAllocs) gAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
float centsFromSemitone (float hz)
{
    const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    return 100.0f * (midi - std::round (midi));
}

struct Stats { float median = 0, p90 = 0, within10 = 0; int n = 0; };
Stats stats (std::vector<float> v)
{
    Stats s;
    s.n = (int) v.size();
    if (v.empty()) return s;
    std::sort (v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.p90 = v[(size_t) std::min (v.size() - 1, (size_t) (0.9 * (double) v.size()))];
    s.within10 = (float) std::count_if (v.begin(), v.end(), [] (float x) { return x <= 10.0f; }) / (float) v.size();
    return s;
}

bool writeWav (const juce::AudioBuffer<float>& b, double sr, const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

bool writePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    return os.openedOk() && png.writeImageToStream (img, os);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::File in, outDir = juce::File::getCurrentWorkingDirectory().getChildFile ("rc_vocal");
    double start = -1.0, seconds = 20.0;
    float tune = 1.0f;
    int style = 0, shots = 4;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        if (a == "--in") in = juce::File (next());
        else if (a == "--out") outDir = juce::File (next());
        else if (a == "--start") start = next().getDoubleValue();
        else if (a == "--seconds") seconds = next().getDoubleValue();
        else if (a == "--tune") tune = next().getFloatValue();
        else if (a == "--style") style = next().getIntValue();
        else if (a == "--shots") shots = next().getIntValue();
    }
    if (! in.existsAsFile()) { std::fprintf (stderr, "usage: --in <wav> --out <dir>\n"); return 2; }
    outDir.createDirectory();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
    if (reader == nullptr) { std::fprintf (stderr, "cannot read %s\n", in.getFullPathName().toRawUTF8()); return 2; }
    const double sr = reader->sampleRate;
    const auto total = reader->lengthInSamples;

    // Default start: the loudest 1 s region, so the excerpt actually has singing.
    if (start < 0.0)
    {
        juce::AudioBuffer<float> scan ((int) reader->numChannels, (int) std::min<juce::int64> (total, (juce::int64) (sr * 600)));
        reader->read (&scan, 0, scan.getNumSamples(), 0, true, true);
        const int win = (int) sr;
        double best = -1.0; int bestAt = 0;
        for (int s0 = 0; s0 + win < scan.getNumSamples(); s0 += win / 2)
        {
            const double r = scan.getRMSLevel (0, s0, win);
            if (r > best) { best = r; bestAt = s0; }
        }
        start = std::max (0.0, bestAt / sr - seconds * 0.4);
    }
    const int n = (int) std::min<juce::int64> ((juce::int64) (seconds * sr), total - (juce::int64) (start * sr));
    juce::AudioBuffer<float> input (2, n);
    {
        juce::AudioBuffer<float> raw ((int) reader->numChannels, n);
        reader->read (&raw, 0, n, (juce::int64) (start * sr), true, true);
        for (int c = 0; c < 2; ++c)
            input.copyFrom (c, 0, raw, std::min (c, raw.getNumChannels() - 1), 0, n);
    }

    constexpr int kBlock = 512;
    auto proc = std::make_unique<RoyalChainAudioProcessor>();
    proc->setRateAndBufferSizeDetails (sr, kBlock);
    proc->prepareToPlay (sr, kBlock);
    auto setParam = [&] (const char* id, float plain) {
        if (auto* p = proc->parameters().getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plain));
    };
    setParam ("tuneKey", 0.0f);
    setParam ("tuneScale", 0.0f);   // Chromatic: nearest semitone, no key assumption
    setParam ("syncFromAutoPitch", 0.0f);
    setParam ("tune", tune);
    setParam ("style", (float) style);
    const int latency = proc->getLatencySamples();

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setSize (1100, 760);
    auto* editor = dynamic_cast<RoyalChainAudioProcessorEditor*> (ed.get());
    auto& compass = editor->viewForTesting().compass();
    compass.prepareForScale (2.0f);

    juce::AudioBuffer<float> output (2, n);
    juce::AudioBuffer<float> block (2, kBlock);
    juce::MidiBuffer midi;
    const int blocksPerFrame = std::max (1, (int) std::round (sr / 60.0 / kBlock));
    const int shotEvery = shots > 0 ? std::max (1, n / (shots + 1)) : n + 1;
    int nextShot = shotEvery, shotIdx = 0;
    double procSec = 0.0;
    long allocs = 0;

    for (int pos = 0, b = 0; pos < n; pos += kBlock, ++b)
    {
        const int len = std::min (kBlock, n - pos);
        block.setSize (2, len, false, false, true);
        for (int c = 0; c < 2; ++c) block.copyFrom (c, 0, input, c, pos, len);
        const auto t0 = std::chrono::steady_clock::now();
        gAllocs = 0; gCountAllocs = true;
        proc->processBlock (block, midi);
        gCountAllocs = false; allocs += gAllocs.load();
        procSec += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        for (int c = 0; c < 2; ++c) output.copyFrom (c, pos, block, c, 0, len);

        if (b % blocksPerFrame == 0)
            compass.advanceFrames (1);
        if (pos >= nextShot && shotIdx < shots)
        {
            const auto& d = compass.dishForTesting();
            std::printf ("SHOT %d @ %.2fs  pitch %.1f Hz  cents %+.1f  match %.2f  loud %.2f  stereo %.3f  mode (%d,%d)\n",
                         shotIdx, start + pos / sr, d.pitchHz(), d.centsOff(), d.tuneMatch(), d.loudness(), d.stereo(),
                         d.dominantMode().n, d.dominantMode().s);
            juce::Image img (juce::Image::ARGB, compass.getWidth() * 2, compass.getHeight() * 2, true);
            { juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale (2.0f)); compass.paintEntireComponent (g, true); }
            writePng (img, outDir.getChildFile ("dish_" + juce::String (shotIdx) + ".png"));
            ++shotIdx;
            nextShot += shotEvery;
        }
    }

    // --- pitch correction on the actual singing ---
    sattari::dsp::PitchDetector detIn, detOut;
    const int hop = 512;
    detIn.prepare (sr, 70.0f, 1000.0f);
    detOut.prepare (sr, 70.0f, 1000.0f);
    std::vector<float> errIn, errOut;
    juce::HeapBlock<float> monoIn (hop), monoOut (hop);
    for (int pos = 0; pos + hop + latency < n; pos += hop)
    {
        for (int i = 0; i < hop; ++i)
        {
            monoIn[i]  = 0.5f * (input.getSample (0, pos + i) + input.getSample (1, pos + i));
            monoOut[i] = 0.5f * (output.getSample (0, pos + i + latency) + output.getSample (1, pos + i + latency));
        }
        detIn.pushBlock (monoIn, hop);
        detOut.pushBlock (monoOut, hop);
        const auto ri = detIn.analyse (0.5f), ro = detOut.analyse (0.5f);
        if (ri.voiced && ro.voiced && ri.frequencyHz > 70.0f && ro.frequencyHz > 70.0f
            && std::abs (std::log2 (ro.frequencyHz / ri.frequencyHz)) < 0.25f)   // same note (no octave slips)
        {
            errIn.push_back (std::abs (centsFromSemitone (ri.frequencyHz)));
            errOut.push_back (std::abs (centsFromSemitone (ro.frequencyHz)));
        }
    }
    const auto si = stats (errIn), so = stats (errOut);

    bool finite = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (output.getSample (c, i))) finite = false;

    writeWav (input, sr, outDir.getChildFile ("input.wav"));
    writeWav (output, sr, outDir.getChildFile ("output_tune" + juce::String (tune, 2) + ".wav"));

    std::printf ("FILE    %s\n", in.getFileName().toRawUTF8());
    std::printf ("SPAN    %.2f s from %.2f s @ %.0f Hz, tune %.2f style %d, latency %d samples (%.2f ms)\n",
                 n / sr, start, sr, tune, style, latency, 1000.0 * latency / sr);
    std::printf ("PITCH   voiced frames %d | |cents from semitone| IN median %.1f p90 %.1f within10 %.0f%% | OUT median %.1f p90 %.1f within10 %.0f%%\n",
                 si.n, si.median, si.p90, 100.0f * si.within10, so.median, so.p90, 100.0f * so.within10);
    std::printf ("OUTPUT  finite %d peak %.3f rms %.4f\n", (int) finite, output.getMagnitude (0, n), output.getRMSLevel (0, 0, n));
    std::printf ("SPEED   %.1fx realtime (whole chain), processBlock allocations %ld\n", (n / sr) / std::max (1e-9, procSec), allocs);

    ed.reset();
    proc->releaseResources();
    return finite && allocs == 0 ? 0 : 1;
}
