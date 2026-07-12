// =============================================================================
//  Sattari Key — REAL-DATASET key-detection evaluator (MIREX weighted score)
// -----------------------------------------------------------------------------
//  Scores the suite's shared key detector against a labeled corpus of real
//  audio (GiantSteps by default) using the standard MIREX weighted metric, for
//  ALL THREE published profile sets (Krumhansl-Schmuckler, Temperley/Kostka-
//  Payne, Aarden-Essen) at once — so the "which profile wins on real music"
//  question is answered on real tracks, not just the synthetic stress cases in
//  key_detector_bench.cpp.
//
//  This mirrors the Sattari Key plugin's ACTUAL analysis chain exactly:
//    * decode WAV -> mono (channel average, same as SongKeyProcessor's mono sum)
//    * 4096-point FFT (fftOrder 12), Hann window w[i] = 0.5 - 0.5*cos(2pi i/(N-1))
//    * performFrequencyOnlyForwardTransform  (juce::dsp::FFT, like the plugin)
//    * accumulateChroma over fftSize/2+1 bins, binHz = sampleRate/fftSize
//    * blendChroma(running, frame, 0.4) rolled across the whole file
//  then runs detail::detectKeyWithProfiles() — the *identical* decision core the
//  plugin's detectKey() uses (pearson over 24 keys + relative-key tiebreak),
//  once per profile pair. The shared header is NOT modified.
//
//  Dataset layout (argv[1] = dataset dir, default validation/giantsteps):
//        <dir>/labels.csv     lines: "<file.wav>,<Key Label>"  e.g. 1004923.wav,C minor
//        <dir>/wav/<file.wav>
//
//  MIREX weighted score per prediction vs ground truth:
//        exact match ........................... 1.0
//        perfect fifth (pred == truth+7, same) . 0.5
//        relative major/minor .................. 0.3
//        parallel major/minor (same root) ...... 0.2
//        else .................................. 0.0
//
//  Needs JUCE (audio decode + FFT); a tool, not a test, so EXCLUDE_FROM_ALL.
//  Runs gracefully (message + exit 0) before any data is fetched.
// =============================================================================

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <sattari_audio_core/SattariChromagram.h>
#include <sattari_audio_core/SattariHpcpChroma.h>
#include <sattari_audio_core/SattariKeyDetector.h>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
// --- Plugin-mirrored analysis constants (see SongKeyProcessor.h) --------------
constexpr int kFftOrder = 12;              // 4096-point, exactly like the plugin
constexpr int kFftSize  = 1 << kFftOrder;  // 4096
constexpr double kBlend = 0.4;             // blendChroma factor used in analyseNow()

static const char* kNoteNames[12] =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// -----------------------------------------------------------------------------
//  Ground-truth label parsing: "<Key Label>" -> (root 0..11, isMinor).
//  Handles sharps/flats + enharmonics (Db=1, Eb=3, Gb=6, Ab=8, Bb=10) and the
//  mode words major/maj/minor/min. "silence"/modal/unparseable -> not parsed
//  (caller counts them as skipped).
// -----------------------------------------------------------------------------
struct Truth { int root = 0; bool minor = false; bool ok = false; };

std::string toLower (std::string s)
{
    for (char& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

Truth parseLabel (const std::string& raw)
{
    Truth t;
    const std::string label = trim (raw);
    if (label.empty()) return t;

    const std::string lower = toLower (label);
    if (lower.find ("silence") != std::string::npos) return t;   // explicitly skip

    // --- letter + accidental -> pitch class ---
    size_t i = 0;
    while (i < label.size() && std::isspace ((unsigned char) label[i])) ++i;
    if (i >= label.size()) return t;

    int base;
    switch (std::toupper ((unsigned char) label[i]))
    {
        case 'C': base = 0;  break;
        case 'D': base = 2;  break;
        case 'E': base = 4;  break;
        case 'F': base = 5;  break;
        case 'G': base = 7;  break;
        case 'A': base = 9;  break;
        case 'B': base = 11; break;
        default:  return t;   // not a note letter -> unparseable
    }
    ++i;
    while (i < label.size() && (label[i] == '#' || label[i] == 'b' || label[i] == 'B'
                               || label[i] == 's' || label[i] == 'S'))  // '#','b','sharp','flat' initials
    {
        const char a = label[i];
        if (a == '#' || a == 's' || a == 'S') base += 1;   // sharp
        else if (a == 'b' || a == 'B')        base -= 1;   // flat (Db, Eb, ...)
        ++i;
        break;   // a single accidental is all GiantSteps labels carry
    }
    t.root = ((base % 12) + 12) % 12;

    // --- mode: "minor"/"min" -> minor, "major"/"maj" -> major, else skip ---
    if (lower.find ("min") != std::string::npos)       { t.minor = true;  t.ok = true; }
    else if (lower.find ("maj") != std::string::npos)  { t.minor = false; t.ok = true; }
    // no recognised mode word (modal: dorian/phrygian/..., or bare root) -> skip
    return t;
}

// -----------------------------------------------------------------------------
//  MIREX weighted scoring.
// -----------------------------------------------------------------------------
enum class Cat { Exact, Fifth, Relative, Parallel, Wrong };

Cat categorise (int predRoot, bool predMinor, int truthRoot, bool truthMinor)
{
    if (predRoot == truthRoot && predMinor == truthMinor)
        return Cat::Exact;
    // perfect fifth: predicted root a fifth above the truth, same mode
    if (predMinor == truthMinor && predRoot == (truthRoot + 7) % 12)
        return Cat::Fifth;
    // relative major/minor: major predicted whose relative minor is the truth,
    // or minor predicted whose relative major is the truth.
    if (! predMinor && truthMinor && truthRoot == (predRoot + 9) % 12)   // pred maj, truth its rel. minor
        return Cat::Relative;
    if (predMinor && ! truthMinor && truthRoot == (predRoot + 3) % 12)   // pred min, truth its rel. major
        return Cat::Relative;
    // parallel major/minor: same root, opposite mode
    if (predRoot == truthRoot && predMinor != truthMinor)
        return Cat::Parallel;
    return Cat::Wrong;
}

double weight (Cat c)
{
    switch (c)
    {
        case Cat::Exact:    return 1.0;
        case Cat::Fifth:    return 0.5;
        case Cat::Relative: return 0.3;
        case Cat::Parallel: return 0.2;
        case Cat::Wrong:    return 0.0;
    }
    return 0.0;
}

struct ProfileScore
{
    const char* name = "";
    int    scored   = 0;
    int    exact    = 0;
    int    fifth    = 0;
    int    relative = 0;
    int    parallel = 0;
    int    wrong    = 0;
    double weightSum = 0.0;
    void add (Cat c)
    {
        ++scored;
        weightSum += weight (c);
        switch (c)
        {
            case Cat::Exact:    ++exact;    break;
            case Cat::Fifth:    ++fifth;    break;
            case Cat::Relative: ++relative; break;
            case Cat::Parallel: ++parallel; break;
            case Cat::Wrong:    ++wrong;    break;
        }
    }
};

// -----------------------------------------------------------------------------
//  Decode a WAV to a mono float buffer (channel average = SongKeyProcessor's
//  mono sum / numChannels). Returns false if the file can't be read.
// -----------------------------------------------------------------------------
bool decodeMono (juce::AudioFormatManager& fm, const juce::File& file,
                 std::vector<float>& mono, double& sampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return false;

    const int numCh = (int) reader->numChannels;
    const juce::int64 len = reader->lengthInSamples;
    if (numCh <= 0 || len <= 0) return false;

    juce::AudioBuffer<float> buf (juce::jmax (1, numCh), (int) len);
    reader->read (&buf, 0, (int) len, 0, true, true);

    sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
    mono.assign ((size_t) len, 0.0f);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* src = buf.getReadPointer (ch);
        for (juce::int64 i = 0; i < len; ++i) mono[(size_t) i] += src[(size_t) i];
    }
    const float inv = 1.0f / (float) numCh;
    for (auto& s : mono) s *= inv;
    return true;
}

// -----------------------------------------------------------------------------
//  Build the whole-file global chroma with the SAME pipeline as the plugin:
//  Hann-windowed 4096-pt FFT, accumulateChroma, blendChroma rolled across the
//  file. Slides a 50%-overlap window (hop = fftSize/2) so every part of the
//  track contributes, exactly as the plugin's rolling histogram would over time.
// -----------------------------------------------------------------------------
void fileChroma (juce::dsp::FFT& fft, const std::array<float, kFftSize>& hann,
                 const std::vector<float>& mono, double sampleRate, double outChroma[12])
{
    std::fill (outChroma, outChroma + 12, 0.0);
    const double binHz = sampleRate / (double) kFftSize;

    std::array<float, kFftSize * 2> scratch {};
    const int hop = kFftSize / 2;
    const juce::int64 n = (juce::int64) mono.size();

    // At least one frame even for short files (zero-padded, like the plugin ring).
    for (juce::int64 start = 0; start == 0 || start + kFftSize <= n; start += hop)
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const juce::int64 idx = start + i;
            const float s = (idx < n) ? mono[(size_t) idx] : 0.0f;
            scratch[(size_t) i] = s * hann[(size_t) i];
        }
        std::fill (scratch.begin() + kFftSize, scratch.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (scratch.data());

        double frame[12] = { 0 };
        sattari::dsp::accumulateChroma (scratch.data(), kFftSize / 2 + 1, binHz, frame);
        sattari::dsp::blendChroma (outChroma, frame, kBlend);

        if (start + kFftSize > n) break;   // short-file single-frame guard
    }
}

// Same frame loop, but feed each magnitude spectrum to the HPCP chromagram
// (tuning-corrected + harmonic-summed + peak/whitened), then finish() to 12 bins.
void fileChromaHpcp (juce::dsp::FFT& fft, const std::array<float, kFftSize>& hann,
                     const std::vector<float>& mono, double sampleRate, double outChroma[12])
{
    const double binHz = sampleRate / (double) kFftSize;
    sattari::dsp::HpcpChromagram hpcp;
    hpcp.reset();

    std::array<float, kFftSize * 2> scratch {};
    const int hop = kFftSize / 2;
    const juce::int64 n = (juce::int64) mono.size();

    for (juce::int64 start = 0; start == 0 || start + kFftSize <= n; start += hop)
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const juce::int64 idx = start + i;
            const float s = (idx < n) ? mono[(size_t) idx] : 0.0f;
            scratch[(size_t) i] = s * hann[(size_t) i];
        }
        std::fill (scratch.begin() + kFftSize, scratch.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (scratch.data());
        hpcp.accumulate (scratch.data(), kFftSize / 2 + 1, binHz);
        if (start + kFftSize > n) break;
    }
    hpcp.finish (outChroma);
}
} // namespace

int main (int argc, char** argv)
{
    using namespace sattari::dsp;

    const std::string datasetArg = (argc > 1) ? argv[1] : "validation/giantsteps";
    const juce::File datasetDir (juce::File::getCurrentWorkingDirectory()
                                     .getChildFile (juce::String (datasetArg)));
    const juce::File labelsFile = datasetDir.getChildFile ("labels.csv");
    const juce::File wavDir     = datasetDir.getChildFile ("wav");

    std::printf ("Sattari Key — real-dataset MIREX evaluation\n");
    std::printf ("Dataset: %s\n\n", datasetDir.getFullPathName().toRawUTF8());

    // --- graceful handling of missing / empty dataset -------------------------
    if (! datasetDir.isDirectory() || ! labelsFile.existsAsFile())
    {
        std::printf ("No dataset found (expected %s + %s).\n",
                     labelsFile.getFullPathName().toRawUTF8(),
                     wavDir.getFullPathName().toRawUTF8());
        std::printf ("Fetch the GiantSteps corpus into that folder, then re-run. Nothing to score.\n");
        return 0;
    }

    juce::StringArray lines;
    labelsFile.readLines (lines);
    if (lines.isEmpty())
    {
        std::printf ("labels.csv is empty. Nothing to score.\n");
        return 0;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();   // WAV + AIFF

    juce::dsp::FFT fft (kFftOrder);
    std::array<float, kFftSize> hann {};
    for (int i = 0; i < kFftSize; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                   * (float) i / (float) (kFftSize - 1));

    struct ProfileDef { const char* name; const double* maj; const double* min; };
    const int NPROF = 5;
    const ProfileDef profiles[NPROF] = {
        { "Krumhansl-Schmuckler",   detail::kKrumhanslMajor, detail::kKrumhanslMinor },
        { "Temperley/Kostka-Payne", detail::kTemperleyMajor, detail::kTemperleyMinor },
        { "Aarden-Essen",           detail::kAardenMajor,    detail::kAardenMinor    },
        { "Sha'ath (audio)",        detail::kShaathMajor,    detail::kShaathMinor    },
        { "Bellman-Budge",          detail::kBellmanMajor,   detail::kBellmanMinor   },
    };
    ProfileScore scores[NPROF];       // current linear-FFT chroma
    ProfileScore scoresHpcp[NPROF];   // new HPCP chroma (tuning-corrected + harmonic-summed)
    for (int i = 0; i < NPROF; ++i)
    {
        scores[i].name     = profiles[i].name;
        scoresHpcp[i].name = profiles[i].name;
    }
    // Store per-track HPCP chroma + truth so we can FIT an empirical profile and score
    // it on a held-out half (honest generalisation, not in-sample).
    std::vector<std::array<double,12>> allChroma;
    std::vector<Truth> allTruth;

    int nTracks = 0, nSkipped = 0, nMissingAudio = 0;

    for (const auto& lineRaw : lines)
    {
        const std::string line = trim (lineRaw.toStdString());
        if (line.empty()) continue;

        const size_t comma = line.find (',');
        if (comma == std::string::npos) { ++nSkipped; continue; }   // malformed / header

        const std::string fileField  = trim (line.substr (0, comma));
        const std::string labelField = trim (line.substr (comma + 1));

        const Truth truth = parseLabel (labelField);
        if (! truth.ok) { ++nSkipped; continue; }                   // silence/modal/unparseable

        const juce::File wav = wavDir.getChildFile (juce::String (fileField));
        if (! wav.existsAsFile()) { ++nMissingAudio; continue; }

        std::vector<float> mono;
        double sampleRate = 44100.0;
        if (! decodeMono (formatManager, wav, mono, sampleRate) || mono.empty())
        {
            ++nMissingAudio;
            continue;
        }

        double chroma[12], chromaHpcp[12];
        fileChroma     (fft, hann, mono, sampleRate, chroma);
        fileChromaHpcp (fft, hann, mono, sampleRate, chromaHpcp);

        ++nTracks;
        for (int p = 0; p < NPROF; ++p)
        {
            const KeyResult k  = detail::detectKeyWithProfiles (chroma,     profiles[p].maj, profiles[p].min);
            const KeyResult kh = detail::detectKeyWithProfiles (chromaHpcp, profiles[p].maj, profiles[p].min);
            scores[p].add     (k.valid  ? categorise (k.root,  k.minor,  truth.root, truth.minor) : Cat::Wrong);
            scoresHpcp[p].add (kh.valid ? categorise (kh.root, kh.minor, truth.root, truth.minor) : Cat::Wrong);
        }
        std::array<double,12> ch; for (int i = 0; i < 12; ++i) ch[(size_t)i] = chromaHpcp[i];
        allChroma.push_back (ch); allTruth.push_back (truth);
    }

    if (nTracks == 0)
    {
        std::printf ("No scorable tracks (parsed labels: 0, missing audio: %d, skipped: %d).\n",
                     nMissingAudio, nSkipped);
        std::printf ("Nothing to score.\n");
        return 0;
    }

    std::printf ("Scored %d tracks  (skipped %d unparseable/silence/modal, %d missing audio)\n\n",
                 nTracks, nSkipped, nMissingAudio);

    auto printBlock = [NPROF] (const char* title, const ProfileScore s[])
    {
        std::printf ("== %s ==\n", title);
        std::printf ("  %-24s  %6s  %8s  %9s   %s\n",
                     "Profile set", "N", "RawAcc", "MIREXx100", "exact/fifth/rel/par/wrong");
        std::printf ("  %-24s  %6s  %8s  %9s   %s\n",
                     "------------------------", "------", "--------", "---------",
                     "-------------------------");
        for (int p = 0; p < NPROF; ++p)
        {
            const double rawAcc = 100.0 * (double) s[p].exact / (double) s[p].scored;
            const double mirex  = 100.0 * s[p].weightSum / (double) s[p].scored;
            std::printf ("  %-24s  %6d  %6.1f %%  %9.2f   %d/%d/%d/%d/%d\n",
                         s[p].name, s[p].scored, rawAcc, mirex,
                         s[p].exact, s[p].fifth, s[p].relative, s[p].parallel, s[p].wrong);
        }
        std::printf ("\n");
    };

    printBlock ("CURRENT chroma (linear FFT)",  scores);
    printBlock ("HPCP chroma (tuning-corrected + harmonic sum)", scoresHpcp);

    // ── Empirical profile: FIT to our own HPCP chroma, scored held-out ───────────
    // Tonic-align each track's chroma by its ground-truth root, average per mode over
    // a TRAIN half, then score on the TEST half (idx%2) so the number is honest, not
    // in-sample. Also print the all-data fit for shipping.
    auto fitProfiles = [&] (bool trainOnEven, std::array<double,12>& maj, std::array<double,12>& mnr) {
        maj.fill (0.0); mnr.fill (0.0); int nMaj = 0, nMin = 0;
        for (size_t t = 0; t < allChroma.size(); ++t) {
            if (trainOnEven && (t % 2 != 0)) continue;          // even = train
            const auto& tr = allTruth[t];
            double sum = 0; for (double v : allChroma[t]) sum += v; if (sum < 1e-9) continue;
            for (int i = 0; i < 12; ++i) {
                const double v = allChroma[t][(size_t)((i + tr.root) % 12)] / sum;   // rotate tonic->0
                if (tr.minor) { mnr[(size_t)i] += v; } else { maj[(size_t)i] += v; }
            }
            tr.minor ? ++nMin : ++nMaj;
        }
        if (nMaj) for (auto& v : maj) v /= nMaj;
        if (nMin) for (auto& v : mnr) v /= nMin;
    };
    std::array<double,12> empMaj, empMin;
    fitProfiles (true, empMaj, empMin);                          // fit on TRAIN (even)
    ProfileScore empScore; empScore.name = "Empirical (HPCP, held-out)";
    for (size_t t = 0; t < allChroma.size(); ++t) {
        if (t % 2 == 0) continue;                                // score on TEST (odd)
        const KeyResult k = detail::detectKeyWithProfiles (allChroma[t].data(), empMaj.data(), empMin.data());
        empScore.add (k.valid ? categorise (k.root, k.minor, allTruth[t].root, allTruth[t].minor) : Cat::Wrong);
    }
    // Refit on ALL data for the shipped constants.
    std::array<double,12> shipMaj, shipMin; fitProfiles (false, shipMaj, shipMin);
    std::printf ("== EMPIRICAL profile (fit to our HPCP chroma) ==\n");
    std::printf ("  held-out MIREXx100 = %.2f   (%d test tracks; exact/fifth/rel/par/wrong %d/%d/%d/%d/%d)\n\n",
                 100.0 * empScore.weightSum / (double) juce::jmax (1, empScore.scored), empScore.scored,
                 empScore.exact, empScore.fifth, empScore.relative, empScore.parallel, empScore.wrong);
    auto printProf = [] (const char* nm, const std::array<double,12>& p) {
        std::printf ("  kEmpirical%s[12] = { ", nm);
        for (int i = 0; i < 12; ++i) std::printf ("%.4f%s", p[(size_t)i], i < 11 ? ", " : " };\n");
    };
    printProf ("Major", shipMaj);
    printProf ("Minor", shipMin);
    std::printf ("\n");

    // Overall winner across all (chroma, profile) combos.
    const char* bestName = scores[0].name; const char* bestChroma = "current";
    double bestMirex = -1.0;
    for (int p = 0; p < NPROF; ++p)
    {
        const double m  = 100.0 * scores[p].weightSum     / (double) scores[p].scored;
        const double mh = 100.0 * scoresHpcp[p].weightSum / (double) scoresHpcp[p].scored;
        if (m  > bestMirex) { bestMirex = m;  bestName = scores[p].name;     bestChroma = "current"; }
        if (mh > bestMirex) { bestMirex = mh; bestName = scoresHpcp[p].name; bestChroma = "HPCP"; }
    }
    std::printf ("  BEST MIREX: %s chroma + %s  (%.2f / 100 over %d tracks)\n",
                 bestChroma, bestName, bestMirex, nTracks);
    (void) kNoteNames;
    return 0;
}
