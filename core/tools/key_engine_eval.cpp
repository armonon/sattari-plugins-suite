// =============================================================================
//  KeyAnalysisEngine evaluator — scores the SHARED engine, not a parallel copy
// -----------------------------------------------------------------------------
//  SattariKeyDatasetEval scores the chroma+profile detector. It has never
//  touched the neural path, which means the 72.1 MIREX figure recorded in
//  SattariKeyNetSpec.h has never been reproduced by anything in this
//  repository. It is a number in a comment.
//
//  This tool measures the neural path and the chroma path THROUGH
//  KeyAnalysisEngine — the same object Compass and Sattari Key are meant to
//  consume — so the comparison is between the things that would actually ship,
//  under one front-end, one scorer, one corpus loader.
//
//  Same MIREX weighting as the existing evaluator, deliberately, so numbers
//  from the two tools can be put side by side:
//      exact 1.0 | fifth 0.5 | relative 0.3 | parallel 0.2 | else 0.0
//
//      key_engine_eval <dataset-dir> [model.onnx]
//      <dir>/labels.csv    "<file.wav>,<Key Label>"
//      <dir>/wav/<file.wav>
// =============================================================================
#include <sattari_audio_core/SattariKeyAnalysis.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace
{
using namespace sattari::dsp;

int noteToPc (const std::string& s)
{
    static const std::map<std::string, int> kMap {
        {"C",0},{"C#",1},{"Db",1},{"D",2},{"D#",3},{"Eb",3},{"E",4},{"Fb",4},
        {"F",5},{"E#",5},{"F#",6},{"Gb",6},{"G",7},{"G#",8},{"Ab",8},{"A",9},
        {"A#",10},{"Bb",10},{"B",11},{"Cb",11},
    };
    const auto it = kMap.find (s);
    return it == kMap.end() ? -1 : it->second;
}

//  MIREX weighting. Identical to key_dataset_eval.cpp so the two tools are
//  directly comparable; duplicated deliberately rather than shared, because a
//  scorer that drifts between tools silently invalidates every comparison made
//  with it.
double mirex (int predRoot, bool predMinor, int trueRoot, bool trueMinor)
{
    if (predRoot == trueRoot && predMinor == trueMinor) return 1.0;
    if (! predMinor && ! trueMinor && predRoot == (trueRoot + 7) % 12) return 0.5;
    if (predMinor && trueMinor && predRoot == (trueRoot + 7) % 12) return 0.5;
    if (! trueMinor && predMinor && predRoot == (trueRoot + 9) % 12) return 0.3;
    if (trueMinor && ! predMinor && predRoot == (trueRoot + 3) % 12) return 0.3;
    if (predRoot == trueRoot && predMinor != trueMinor) return 0.2;
    return 0.0;
}

struct Row { std::string file; int root; bool minor; };

std::vector<Row> loadLabels (const juce::File& csv)
{
    std::vector<Row> rows;
    juce::StringArray lines;
    csv.readLines (lines);
    for (const auto& raw : lines)
    {
        const auto line = raw.trim();
        if (line.isEmpty()) continue;
        const int comma = line.indexOfChar (',');
        if (comma < 0) continue;
        const auto file = line.substring (0, comma).trim();
        auto label = line.substring (comma + 1).trim();

        const int sp = label.indexOfChar (' ');
        if (sp < 0) continue;
        const int pc = noteToPc (label.substring (0, sp).toStdString());
        const bool minor = label.substring (sp + 1).trim().toLowerCase().startsWith ("min");
        if (pc < 0) continue;
        rows.push_back ({ file.toStdString(), pc, minor });
    }
    return rows;
}

struct Tally
{
    int n = 0, exact = 0, fifth = 0, rel = 0, par = 0, wrong = 0;
    double sum = 0.0;
    //  Confidence buckets, for the calibration question: does a higher reported
    //  confidence actually mean a higher chance of being right?
    int bucketN[5] {}, bucketHit[5] {};

    void add (double score, double conf, bool isExact)
    {
        ++n; sum += score;
        if (score >= 1.0) ++exact;
        else if (score >= 0.5) ++fifth;
        else if (score >= 0.3) ++rel;
        else if (score >= 0.2) ++par;
        else ++wrong;
        const int b = std::min (4, std::max (0, (int) (conf * 5.0)));
        ++bucketN[b]; if (isExact) ++bucketHit[b];
    }

    void print (const char* label) const
    {
        if (n == 0) { std::printf ("  %-26s   no results\n", label); return; }
        std::printf ("  %-26s %6d  %7.1f %%  %9.2f   %d/%d/%d/%d/%d\n",
                     label, n, 100.0 * exact / n, 100.0 * sum / n,
                     exact, fifth, rel, par, wrong);
    }

    void printCalibration (const char* label) const
    {
        std::printf ("\n  confidence calibration — %s\n", label);
        std::printf ("    %-14s %8s %10s\n", "reported", "n", "exact %");
        static const char* names[5] = { "0.0-0.2", "0.2-0.4", "0.4-0.6", "0.6-0.8", "0.8-1.0" };
        for (int b = 0; b < 5; ++b)
            if (bucketN[b] > 0)
                std::printf ("    %-14s %8d %9.1f %%\n", names[b], bucketN[b],
                             100.0 * bucketHit[b] / bucketN[b]);
        std::printf ("    (a confidence with meaning shows exact %% RISING down this table)\n");
    }
};
} // namespace

int main (int argc, char** argv)
{
    const juce::File dir (juce::String::fromUTF8 (argc > 1 ? argv[1] : "validation/giantsteps"));
    const auto rows = loadLabels (dir.getChildFile ("labels.csv"));
    if (rows.empty()) { std::fprintf (stderr, "no labels in %s\n", dir.getFullPathName().toRawUTF8()); return 2; }

    KeyAnalysisEngine engine;
    bool haveModel = false;
    if (argc > 2)
        haveModel = engine.loadNeuralModel (argv[2]);
    else
    {
        const auto def = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("Sattari Audio").getChildFile ("models")
                             .getChildFile ("keynet_spec.onnx");
        if (def.existsAsFile()) haveModel = engine.loadNeuralModel (def.getFullPathName().toStdString());
    }

    std::printf ("KeyAnalysisEngine evaluation\n");
    std::printf ("  dataset : %s  (%d labelled tracks)\n", dir.getFileName().toRawUTF8(), (int) rows.size());
    std::printf ("  model   : %s\n\n", haveModel ? "keynet_spec.onnx LOADED" : "NOT AVAILABLE (chroma only)");

    struct Cfg { const char* name; KeyAnalysisConfig cfg; Tally t; };
    std::vector<Cfg> runs {
        { "chroma Aarden (was live)", { KeyProfile::Aarden,    false }, {} },
        { "chroma Krumhansl",         { KeyProfile::Krumhansl, false }, {} },
        { "chroma Temperley",         { KeyProfile::Temperley, false }, {} },
        { "chroma Sha'ath",           { KeyProfile::Shaath,    false }, {} },
        { "chroma Bellman",           { KeyProfile::Bellman,   false }, {} },
    };
    if (haveModel)
        runs.push_back ({ "NEURAL keynet_spec", { KeyProfile::Aarden, true }, {} });

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    int decoded = 0;

    for (const auto& row : rows)
    {
        const auto f = dir.getChildFile ("wav").getChildFile (juce::String (row.file));
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) continue;

        juce::AudioBuffer<float> buf ((int) juce::jmax (1u, rd->numChannels),
                                      (int) rd->lengthInSamples);
        rd->read (&buf, 0, (int) rd->lengthInSamples, 0, true, true);

        //  Mono sum, matching what both plugins feed their analysers.
        std::vector<float> mono ((size_t) buf.getNumSamples(), 0.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
                mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();

        //  The chroma is front-end work and is identical for every profile, so
        //  compute it ONCE and score the profiles against it. Recomputing per
        //  profile would multiply a 500-track run by five for no new evidence.
        const auto chroma = engine.rollingChroma (mono.data(), (int) mono.size(), rd->sampleRate);

        for (auto& r : runs)
        {
            KeyAnalysisResult res;
            if (r.cfg.useNeural)
                res = engine.analyse (mono.data(), (int) mono.size(), rd->sampleRate, r.cfg);
            else
                res = engine.analyseChroma (chroma.data(), r.cfg);
            if (! res.valid) continue;
            const double s = mirex (res.root, res.minor, row.root, row.minor);
            r.t.add (s, res.confidence, s >= 1.0);
        }
        ++decoded;
        if (decoded % 100 == 0) { std::printf ("  ... %d\n", decoded); std::fflush (stdout); }
    }

    std::printf ("\n  %-26s %6s  %9s %9s   %s\n", "path", "N", "RawAcc", "MIREXx100",
                 "exact/fifth/rel/par/wrong");
    std::printf ("  %-26s %6s  %9s %9s   %s\n", "--------------------------", "------",
                 "--------", "---------", "-------------------------");
    for (const auto& r : runs) r.t.print (r.name);

    //  Calibration only for the paths a conductor would gate on.
    for (const auto& r : runs)
        if (r.cfg.useNeural || r.cfg.profile == KeyProfile::Shaath)
            r.t.printCalibration (r.name);

    return 0;
}
