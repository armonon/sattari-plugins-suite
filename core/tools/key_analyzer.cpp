// Sattari Key — standalone batch key + BPM analyzer.
//
// Points the deployed neural key model (keynet.onnx, HPCP chroma + TTA, 72 MIREX) at a
// FOLDER of audio and writes a DJ-ready report: key, Camelot code, BPM, confidence.
// This is the "packaging, not research" product — the model is already trained; this
// turns it into the thing DJs pay Mixed In Key ($58-98) for.
//
//   key_analyzer <folder> [--csv out.csv] [--rekordbox out.xml]
//
// --rekordbox writes a rekordbox-importable collection XML (key + BPM + Camelot), so a
// DJ can analyse a whole library here and import it with the key column already filled —
// the paid feature of Mixed In Key / rekordbox's own analysis.
// Reads models from ~/Library/Sattari Audio/models/keynet.onnx (same as the plugins).
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariKeyNet.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr int    kFft = 4096, kHop = 2048;
const char* kNote[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
// Camelot wheel numbers by pitch class (0=C..11=B); letter B=major, A=minor.
constexpr int kCamMaj[12] = { 8,3,10,5,12,7,2,9,4,11,6,1 };
constexpr int kCamMin[12] = { 5,12,7,2,9,4,11,6,1,8,3,10 };

struct Analysis { juce::String file; juce::File src; int root = 0; bool minor = false; float conf = 0; double bpm = 0; };
double estimateBpm (const std::vector<float>& env, double envRateHz);   // fwd

// Whole-file HPCP mean(12)+cov(78) — exactly the model's training feature — plus a
// spectral-flux onset envelope reused for tempo.
bool analyseFile (juce::AudioFormatManager& fm, const juce::File& f, sattari::dsp::KeyNet& net, Analysis& a)
{
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr || rd->lengthInSamples < kFft) return false;
    const double sr = rd->sampleRate;
    const int total = (int) juce::jmin (rd->lengthInSamples, (juce::int64) (sr * 300.0));   // cap 5 min
    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, rd->numChannels), total);
    rd->read (&buf, 0, total, 0, true, true);

    juce::dsp::FFT fft ((int) std::log2 ((double) kFft));
    std::vector<float> scratch ((size_t) kFft * 2, 0.0f), hann ((size_t) kFft), prevMag ((size_t) (kFft / 2 + 1), 0.0f);
    for (int i = 0; i < kFft; ++i) hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (kFft - 1));

    std::array<double,12> mean {}; double cov[12][12] = {}; int frames = 0;
    std::vector<float> onsetEnv; onsetEnv.reserve ((size_t) (total / kHop + 1));
    sattari::dsp::HpcpChromagram hpcp;
    const double binHz = sr / kFft;

    for (int pos = 0; pos + kFft <= total; pos += kHop)
    {
        for (int i = 0; i < kFft; ++i)
        {
            double m = 0; for (int ch = 0; ch < buf.getNumChannels(); ++ch) m += buf.getSample (ch, pos + i);
            scratch[(size_t) i] = (float) (m / buf.getNumChannels()) * hann[(size_t) i];
        }
        std::fill (scratch.begin() + kFft, scratch.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (scratch.data());
        // onset flux
        float flux = 0; for (int b = 0; b < kFft / 2 + 1; ++b) { const float d = scratch[(size_t) b] - prevMag[(size_t) b]; if (d > 0) flux += d; prevMag[(size_t) b] = scratch[(size_t) b]; }
        onsetEnv.push_back (flux);
        // chroma
        std::array<double,12> ch {}; hpcp.reset(); hpcp.accumulate (scratch.data(), kFft / 2 + 1, binHz); hpcp.finish (ch.data());
        double s = 0; for (double v : ch) s += v; if (s < 1e-9) continue;
        for (auto& v : ch) v /= s; ++frames;
        for (int i = 0; i < 12; ++i) { mean[(size_t) i] += ch[(size_t) i]; for (int j = 0; j < 12; ++j) cov[i][j] += ch[(size_t) i] * ch[(size_t) j]; }
    }
    if (frames == 0) return false;
    for (auto& v : mean) v /= frames;
    for (int i = 0; i < 12; ++i) for (int j = 0; j < 12; ++j) cov[i][j] = cov[i][j] / frames - mean[(size_t) i] * mean[(size_t) j];
    float feat[90]; int fi = 0;
    for (int i = 0; i < 12; ++i) feat[fi++] = (float) mean[(size_t) i];
    for (int i = 0; i < 12; ++i) for (int j = i; j < 12; ++j) feat[fi++] = (float) cov[i][j];

    const auto kn = net.available() ? net.detectTTA (feat) : sattari::dsp::KeyNetResult{};
    a.file = f.getFileName(); a.src = f; a.root = kn.root; a.minor = kn.minor; a.conf = kn.confidence;
    a.bpm = estimateBpm (onsetEnv, sr / kHop);
    return true;
}

// Autocorrelation tempo over the flux onset envelope, folded into 70-160 BPM.
double estimateBpm (const std::vector<float>& env, double envRateHz)
{
    const int n = (int) env.size(); if (n < 8) return 0.0;
    double mean = 0; for (float v : env) mean += v; mean /= n;
    std::vector<float> e ((size_t) n); for (int i = 0; i < n; ++i) e[(size_t) i] = env[(size_t) i] - (float) mean;
    const int loLag = (int) (envRateHz * 60.0 / 200.0), hiLag = (int) (envRateHz * 60.0 / 60.0);
    double best = -1; int bestLag = loLag;
    for (int lag = loLag; lag <= hiLag && lag < n; ++lag)
    {
        double acc = 0; for (int i = 0; i + lag < n; ++i) acc += (double) e[(size_t) i] * e[(size_t) (i + lag)];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    double bpm = 60.0 * envRateHz / bestLag;
    while (bpm < 70.0)  bpm *= 2.0;
    while (bpm > 160.0) bpm /= 2.0;
    return bpm;
}

// ── rekordbox collection export ───────────────────────────────────────────────
// rekordbox reads the Tonality attribute for its Key column; we also stash the
// Camelot code in Comments (Mixed In Key convention) for harmonic-mixing sorting.
juce::String rbTonality (int root, bool minor)
{
    return juce::String (kNote[((root % 12) + 12) % 12]) + (minor ? "m" : "");
}

// rekordbox Location is a file URL of the form file://localhost/abs/path (percent-
// encoded). juce::URL(File) yields file:///abs/path — swap in the localhost authority.
juce::String fileLocation (const juce::File& f)
{
    auto u = juce::URL (f).toString (false);
    if (u.startsWith ("file://")) u = "file://localhost" + u.substring (7);
    return u;
}

void writeRekordbox (const juce::File& out, const std::vector<Analysis>& rows)
{
    juce::XmlElement root ("DJ_PLAYLISTS");
    root.setAttribute ("Version", "1.0.0");
    auto* product = root.createNewChildElement ("PRODUCT");
    product->setAttribute ("Name", "Sattari Key");
    product->setAttribute ("Version", "1.0");
    product->setAttribute ("Company", "Sattari Audio");

    auto* col = root.createNewChildElement ("COLLECTION");
    col->setAttribute ("Entries", (int) rows.size());
    int id = 1;
    for (const auto& a : rows)
    {
        const int r = ((a.root % 12) + 12) % 12;
        auto* t = col->createNewChildElement ("TRACK");
        t->setAttribute ("TrackID", id++);
        t->setAttribute ("Name", a.src.getFileNameWithoutExtension());
        t->setAttribute ("Kind", a.src.getFileExtension().substring (1).toUpperCase() + " File");
        t->setAttribute ("Location", fileLocation (a.src));
        t->setAttribute ("AverageBpm", juce::String (a.bpm, 2));
        t->setAttribute ("Tonality", rbTonality (r, a.minor));
        t->setAttribute ("Comments", juce::String (a.minor ? kCamMin[r] : kCamMaj[r]) + (a.minor ? "A" : "B"));
    }

    // A minimal (empty) PLAYLISTS/ROOT node keeps rekordbox's importer happy.
    auto* pls = root.createNewChildElement ("PLAYLISTS");
    auto* node = pls->createNewChildElement ("NODE");
    node->setAttribute ("Type", "0"); node->setAttribute ("Name", "ROOT"); node->setAttribute ("Count", "0");

    out.replaceWithText (root.toString());
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: key_analyzer <folder> [--csv out.csv] [--rekordbox out.xml]\n"); return 2; }
    juce::File folder (juce::String::fromUTF8 (argv[1]));
    if (! folder.isDirectory()) { std::fprintf (stderr, "not a folder: %s\n", argv[1]); return 2; }
    juce::File csv, rbxml;
    for (int i = 2; i < argc - 1; ++i)
    {
        if (juce::String (argv[i]) == "--csv")       csv   = juce::File (juce::String::fromUTF8 (argv[i + 1]));
        if (juce::String (argv[i]) == "--rekordbox") rbxml = juce::File (juce::String::fromUTF8 (argv[i + 1]));
    }

    sattari::dsp::KeyNet net;
    auto model = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("Sattari Audio").getChildFile ("models").getChildFile ("keynet.onnx");
    const bool haveModel = model.existsAsFile() && net.loadModel (model.getFullPathName().toStdString());
    if (! haveModel) std::fprintf (stderr, "WARNING: keynet.onnx not found/loaded (%s) — keys will be blank.\n", model.getFullPathName().toRawUTF8());

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto files = folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.m4a;*.ogg");
    files.sort();

    std::printf ("%-40s  %-9s  %-4s  %-4s  %s\n", "file", "key", "cam", "bpm", "conf");
    std::printf ("%-40s  %-9s  %-4s  %-4s  %s\n", "----", "---", "---", "---", "----");
    juce::StringArray csvRows; csvRows.add ("file,key,camelot,bpm,confidence");
    std::vector<Analysis> rows;
    int ok = 0;
    for (const auto& f : files)
    {
        Analysis a;
        if (! analyseFile (fm, f, net, a)) { std::printf ("%-40s  (unreadable)\n", f.getFileName().toRawUTF8()); continue; }
        const juce::String key = juce::String (kNote[((a.root % 12) + 12) % 12]) + (a.minor ? " min" : " maj");
        const juce::String cam = juce::String (a.minor ? kCamMin[a.root] : kCamMaj[a.root]) + (a.minor ? "A" : "B");
        std::printf ("%-40s  %-9s  %-4s  %-4d  %.0f%%\n",
                     f.getFileName().substring (0, 40).toRawUTF8(), key.toRawUTF8(), cam.toRawUTF8(),
                     (int) std::lround (a.bpm), a.conf * 100.0f);
        csvRows.add (a.file.replaceCharacter (',', ' ') + "," + key + "," + cam + "," + juce::String ((int) std::lround (a.bpm)) + "," + juce::String (a.conf, 3));
        rows.push_back (a);
        ++ok;
    }
    std::printf ("\nanalysed %d file(s).\n", ok);
    if (csv != juce::File{}) { csv.replaceWithText (csvRows.joinIntoString ("\n") + "\n"); std::printf ("wrote %s\n", csv.getFullPathName().toRawUTF8()); }
    if (rbxml != juce::File{}) { writeRekordbox (rbxml, rows); std::printf ("wrote %s (%d tracks, rekordbox import)\n", rbxml.getFullPathName().toRawUTF8(), (int) rows.size()); }
    return 0;
}
