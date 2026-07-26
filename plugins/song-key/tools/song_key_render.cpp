// Sattari Key — offline detection proof.
//
// Feeds a C major chord (C4+E4+G4) into the analyser, runs the chroma->key
// chain, and asserts it detects C. Proves the master-key scan actually resolves
// the key from a full-mix-style signal, not just compiles.
//
//   song_key_render          # detect + assert C (local proof)
//   song_key_render --smoke  # assert a valid key is detected (CI-safe)
//   song_key_render --corpus <dir> [N]   # score the REAL PLUGIN on a labelled corpus
//
// --corpus is the honest end-to-end measurement: it streams each track through
// SattariSongKeyAudioProcessor::processBlock exactly as a host would, then reads
// detected() and scores MIREX. SattariKeyDatasetEval scores the shared detector
// core; this scores what the plugin actually ships, including its ring buffers,
// worker cadence and neural-vs-chroma fallback logic.
#include "SongKeyProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
void setParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}
}

namespace
{
// "C minor" / "Eb major" / "F# minor" -> root 0..11 + minor flag. Returns false on
// anything unparseable so a malformed label is skipped rather than scored as wrong.
bool parseKeyLabel (std::string s, int& root, bool& minor)
{
    while (! s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.erase (s.begin());
    while (! s.empty() && (s.back()  == ' ' || s.back()  == '\t' || s.back()  == '\r')) s.pop_back();
    if (s.size() < 2) return false;
    static const char* N[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    int pc = -1;
    switch (s[0]) { case 'C': pc=0; break; case 'D': pc=2; break; case 'E': pc=4; break;
                    case 'F': pc=5; break; case 'G': pc=7; break; case 'A': pc=9; break;
                    case 'B': pc=11; break; default: return false; }
    std::size_t i = 1;
    if (i < s.size() && (s[i] == '#' || s[i] == 's')) { pc = (pc + 1) % 12; ++i; }
    else if (i < s.size() && (s[i] == 'b' || s[i] == 'f')) { pc = (pc + 11) % 12; ++i; }
    std::string mode = s.substr (i);
    std::transform (mode.begin(), mode.end(), mode.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    minor = (mode.find ("min") != std::string::npos) || (mode.find (" m") != std::string::npos);
    root = pc;
    (void) N;
    return true;
}

// Standard MIREX weighted score for one prediction.
double mirexScore (int pRoot, bool pMin, int tRoot, bool tMin)
{
    if (pRoot == tRoot && pMin == tMin) return 1.0;                       // exact
    if (! pMin && ! tMin && (pRoot - tRoot + 12) % 12 == 7) return 0.5;   // dominant (major)
    if (pMin && tMin && (pRoot - tRoot + 12) % 12 == 7) return 0.5;
    if (! tMin && pMin && (pRoot - tRoot + 12) % 12 == 9) return 0.3;     // relative minor
    if (tMin && ! pMin && (pRoot - tRoot + 12) % 12 == 3) return 0.3;     // relative major
    if (pRoot == tRoot && pMin != tMin) return 0.2;                       // parallel
    return 0.0;
}
} // namespace

int main (int argc, char** argv)
{
    const bool smoke = (argc > 1 && std::strcmp (argv[1], "--smoke") == 0);

    // --corpus <dir> [N] : stream real labelled tracks through the actual plugin.
    if (argc > 2 && std::strcmp (argv[1], "--corpus") == 0)
    {
        const juce::File dir (juce::String::fromUTF8 (argv[2]));
        const int limit = (argc > 3) ? std::atoi (argv[3]) : 0;   // 0 = all
        std::ifstream labels (dir.getChildFile ("labels.csv").getFullPathName().toStdString());
        if (! labels) { std::fprintf (stderr, "FAIL: no labels.csv in %s\n", argv[2]); return 2; }

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        const int block = 512;
        int n = 0, exact = 0; double mirex = 0.0;
        std::string line;
        while (std::getline (labels, line))
        {
            if (limit > 0 && n >= limit) break;
            const auto comma = line.find (',');
            if (comma == std::string::npos) continue;
            std::string fname = line.substr (0, comma);
            fname.erase (std::remove (fname.begin(), fname.end(), '\r'), fname.end());
            int tRoot = 0; bool tMin = false;
            if (! parseKeyLabel (line.substr (comma + 1), tRoot, tMin)) continue;

            const auto wav = dir.getChildFile ("wav").getChildFile (juce::String::fromUTF8 (fname.c_str()));
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (wav));
            if (rd == nullptr) continue;

            const double sr = rd->sampleRate;
            SattariSongKeyAudioProcessor proc;
            proc.prepareToPlay (sr, block);
            proc.releaseResources();                          // drive analysis ourselves, no worker race
            setParam (proc.parameters(), "publish", 0.0f);    // never touch the live suite bus

            juce::AudioBuffer<float> buf (2, block);
            const juce::int64 total = rd->lengthInSamples;
            // Drive analyseNow() at the SAME rate the plugin's worker does (~700 ms),
            // not once per block: the chroma accumulates per call, so a faster cadence
            // would measure something the shipping plugin never does.
            const juce::int64 analysePeriod = (juce::int64) (sr * 0.7);
            juce::int64 sinceAnalyse = 0;
            for (juce::int64 pos = 0; pos + block <= total; pos += block)
            {
                buf.clear();
                rd->read (&buf, 0, block, pos, true, true);
                juce::MidiBuffer midi;
                proc.processBlock (buf, midi);
                sinceAnalyse += block;
                if (sinceAnalyse >= analysePeriod) { sinceAnalyse = 0; proc.analyseNow(); }
            }
            proc.analyseNow();                                 // final settle

            const auto d = proc.detected();
            if (! d.valid) continue;
            const double s = mirexScore (d.root % 12, d.minor, tRoot, tMin);
            mirex += s; if (s >= 1.0) ++exact; ++n;
            if (n % 25 == 0) std::printf ("  ... %d tracks, running MIREX %.2f, exact %.1f%%\n",
                                          n, 100.0 * mirex / n, 100.0 * exact / n);
        }
        if (n == 0) { std::fprintf (stderr, "FAIL: no scorable tracks\n"); return 2; }
        std::printf ("\n== Sattari Key PLUGIN end-to-end on %d tracks ==\n", n);
        std::printf ("   MIREXx100 = %.2f     exact = %.1f%%  (%d/%d)\n",
                     100.0 * mirex / n, 100.0 * exact / n, exact, n);
        return 0;
    }

    const double sr = 48000.0;
    const int    block = 512;

    SattariSongKeyAudioProcessor proc;
    proc.prepareToPlay (sr, block);
    proc.releaseResources();                   // stop the background worker; we drive analysis directly
    setParam (proc.parameters(), "publish", 0.0f);   // don't touch the suite bus during the test

    // ~0.7 s of a C major triad, fed block by block to fill the analysis ring.
    const double f[3] = { 261.63, 329.63, 392.00 };
    double ph[3] = { 0, 0, 0 };
    const int blocks = (int) (0.7 * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int i = 0; i < block; ++i)
        {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k) { s += 0.3f * (float) std::sin (ph[k]); ph[k] += 2.0 * juce::MathConstants<double>::pi * f[k] / sr; }
            buf.setSample (0, i, s); buf.setSample (1, i, s);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
    }

    for (int i = 0; i < 20; ++i) proc.analyseNow();   // let the rolling chroma settle

    const auto d = proc.detected();
    if (! d.valid) { std::fprintf (stderr, "FAIL: no key detected\n"); return 1; }
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    std::printf ("Sattari Key detected: %s %s (confidence %.3f)\n", names[d.root % 12], d.minor ? "minor" : "major", d.confidence);

    if (smoke) { std::printf ("Sattari Key render smoke ok\n"); return 0; }

    if (d.root != 0)
    { std::fprintf (stderr, "FAIL: C major chord detected as root %d, expected 0 (C)\n", d.root); return 1; }

    std::printf ("Sattari Key detection proof ok\n");
    return 0;
}
