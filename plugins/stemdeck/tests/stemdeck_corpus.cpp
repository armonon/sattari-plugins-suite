// Manifest-driven real-music accuracy gate.
//
// This executable deliberately contains no generated fixtures. It only passes
// when given a corpus manifest whose entries point at real, full-length audio
// and whose expected BPM/key/meter were established independently.
//
//   StemDeckCorpusGate path/to/corpus.json [report.json]
//
// Manifest schema (v1):
// {
//   "version": 1,
//   "minimumTracks": 5,
//   "minimumMinutes": 20,
//   "tracks": [{
//     "file": "audio/song.wav",
//     "label": "artist - title",
//     "sourceType": "real_music",
//     "expectedBpm": 124.0,
//     "bpmTolerance": 2.0,
//     "expectedKeyPitchClass": 9,
//     "expectedKeyMinor": true,
//     "expectedMeter": 4,
//     "minimumDurationSeconds": 120
//   }]
// }

#include "TrackAnalysis.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <memory>

namespace
{
double number (const juce::DynamicObject& o, const juce::Identifier& key, double fallback)
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : (double) v;
}

int integer (const juce::DynamicObject& o, const juce::Identifier& key, int fallback)
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : (int) v;
}

bool boolean (const juce::DynamicObject& o, const juce::Identifier& key, bool fallback)
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : (bool) v;
}

juce::String text (const juce::DynamicObject& o, const juce::Identifier& key,
                   const juce::String& fallback = {})
{
    const auto v = o.getProperty (key);
    return v.isVoid() ? fallback : v.toString();
}

juce::var resultObject (const juce::String& label, const juce::File& file,
                        const TrackAnalysis& analysis, double duration,
                        bool bpmOk, bool keyOk, bool meterOk, bool durationOk)
{
    auto* r = new juce::DynamicObject();
    r->setProperty ("label", label);
    r->setProperty ("file", file.getFullPathName());
    r->setProperty ("durationSeconds", duration);
    r->setProperty ("detectedBpm", analysis.detectedBpm);
    r->setProperty ("detectedKeyPitchClass", analysis.keyPitchClass);
    r->setProperty ("detectedKeyMinor", analysis.keyIsMinor);
    r->setProperty ("keyConfidence", analysis.keyConfidence);
    r->setProperty ("detectedMeter", analysis.beatsPerBar);
    r->setProperty ("bpmPass", bpmOk);
    r->setProperty ("keyPass", keyOk);
    r->setProperty ("meterPass", meterOk);
    r->setProperty ("durationPass", durationOk);
    r->setProperty ("pass", bpmOk && keyOk && meterOk && durationOk);
    return juce::var (r);
}
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: StemDeckCorpusGate corpus.json [report.json]\n";
        return 2;
    }

    const juce::File manifest (juce::String::fromUTF8 (argv[1]));
    if (! manifest.existsAsFile())
    {
        std::cerr << "corpus manifest does not exist: " << manifest.getFullPathName() << "\n";
        return 2;
    }

    const auto root = juce::JSON::parse (manifest);
    auto* obj = root.getDynamicObject();
    if (obj == nullptr || integer (*obj, "version", 0) != 1)
    {
        std::cerr << "corpus manifest must be a version-1 JSON object\n";
        return 2;
    }

    const auto tracks = obj->getProperty ("tracks");
    auto* entries = tracks.getArray();
    if (entries == nullptr)
    {
        std::cerr << "corpus manifest has no tracks array\n";
        return 2;
    }

    const int minimumTracks = integer (*obj, "minimumTracks", 5);
    const double minimumMinutes = number (*obj, "minimumMinutes", 20.0);
    int failures = 0;
    int realTracks = 0;
    double totalSeconds = 0.0;
    juce::Array<juce::var> results;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (const auto& entry : *entries)
    {
        auto* e = entry.getDynamicObject();
        if (e == nullptr)
        {
            ++failures;
            std::cerr << "FAIL: corpus entry is not an object\n";
            continue;
        }

        const auto relative = text (*e, "file");
        const auto file = juce::File::isAbsolutePath (relative)
                        ? juce::File (relative)
                        : manifest.getParentDirectory().getChildFile (relative);
        const auto label = text (*e, "label", file.getFileNameWithoutExtension());
        const bool declaredReal = text (*e, "sourceType") == "real_music";
        if (! declaredReal)
        {
            ++failures;
            std::cerr << "FAIL: " << label << " is not truth-labelled sourceType=real_music\n";
            continue;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 1 || reader->sampleRate <= 0.0)
        {
            ++failures;
            std::cerr << "FAIL: cannot decode " << file.getFullPathName() << "\n";
            continue;
        }
        if (reader->lengthInSamples > (juce::int64) std::numeric_limits<int>::max())
        {
            ++failures;
            std::cerr << "FAIL: track is too long for the current analysis buffer: " << label << "\n";
            continue;
        }

        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        if (! reader->read (&audio, 0, audio.getNumSamples(), 0, true, true))
        {
            ++failures;
            std::cerr << "FAIL: decode failed for " << label << "\n";
            continue;
        }

        const double duration = (double) reader->lengthInSamples / reader->sampleRate;
        const auto analysis = TrackAnalyzer::analyze (audio, reader->sampleRate);
        const double expectedBpm = number (*e, "expectedBpm", -1.0);
        const double bpmTolerance = number (*e, "bpmTolerance", 2.0);
        const int expectedKey = integer (*e, "expectedKeyPitchClass", -1);
        const bool expectedMinor = boolean (*e, "expectedKeyMinor", false);
        const int expectedMeter = integer (*e, "expectedMeter", 0);
        const double minimumDuration = number (*e, "minimumDurationSeconds", 120.0);

        const bool bpmOk = expectedBpm > 0.0
                        && std::abs ((double) analysis.detectedBpm - expectedBpm) <= bpmTolerance;
        const bool keyOk = expectedKey < 0
                        || (analysis.keyPitchClass == expectedKey && analysis.keyIsMinor == expectedMinor);
        const bool meterOk = expectedMeter <= 0 || analysis.beatsPerBar == expectedMeter;
        const bool durationOk = duration >= minimumDuration;
        const bool pass = bpmOk && keyOk && meterOk && durationOk;
        failures += pass ? 0 : 1;
        ++realTracks;
        totalSeconds += duration;
        results.add (resultObject (label, file, analysis, duration, bpmOk, keyOk, meterOk, durationOk));

        std::cout << (pass ? "PASS " : "FAIL ") << label
                  << " bpm=" << juce::String (analysis.detectedBpm, 2)
                  << " key=" << analysis.keyName()
                  << " meter=" << analysis.beatsPerBar << "/4"
                  << " duration=" << juce::String (duration, 1) << "s\n";
    }

    if (realTracks < minimumTracks)
    {
        ++failures;
        std::cerr << "FAIL: corpus has " << realTracks << " real tracks; requires "
                  << minimumTracks << "\n";
    }
    if (totalSeconds < minimumMinutes * 60.0)
    {
        ++failures;
        std::cerr << "FAIL: corpus has " << juce::String (totalSeconds / 60.0, 1)
                  << " minutes; requires " << juce::String (minimumMinutes, 1) << "\n";
    }

    auto* report = new juce::DynamicObject();
    report->setProperty ("schema", 1);
    report->setProperty ("manifest", manifest.getFullPathName());
    report->setProperty ("realTracks", realTracks);
    report->setProperty ("totalMinutes", totalSeconds / 60.0);
    report->setProperty ("failures", failures);
    report->setProperty ("pass", failures == 0);
    report->setProperty ("results", juce::var (results));
    const auto reportJson = juce::JSON::toString (juce::var (report), true);

    if (argc >= 3)
    {
        const juce::File reportFile (juce::String::fromUTF8 (argv[2]));
        reportFile.getParentDirectory().createDirectory();
        if (! reportFile.replaceWithText (reportJson))
        {
            std::cerr << "could not write report: " << reportFile.getFullPathName() << "\n";
            return 2;
        }
    }

    std::cout << (failures == 0 ? "REAL_CORPUS_OK" : "REAL_CORPUS_FAIL")
              << " tracks=" << realTracks
              << " minutes=" << juce::String (totalSeconds / 60.0, 1)
              << " failures=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}
