#pragma once

#include <JuceHeader.h>
#include <vector>
#include "MelodyExtractor.h"

// =============================================================================
//  NeuralTranscriber — polyphonic audio→MIDI via Spotify's basic-pitch CLI.
// -----------------------------------------------------------------------------
//  Same adapter pattern as StemProvider/demucs: an external neural tool is
//  auto-detected and shelled out to on a BACKGROUND thread; when it is not
//  installed the caller falls back to the built-in monophonic YIN extractor.
//
//    basic-pitch <output-dir> <input-audio>   →  <stem>_basic_pitch.mid
//
//  Enable with:  python3 -m pip install --user basic-pitch
//  Detection order: SATTARI_STEMDECK_TRANSCRIBER_CMD env var → config file →
//  well-known install paths → PATH lookup.
// =============================================================================
namespace NeuralTranscriber
{
    inline juce::String detectCommand()
    {
        auto cmd = juce::SystemStats::getEnvironmentVariable ("SATTARI_STEMDECK_TRANSCRIBER_CMD", {});
        if (cmd.trim().isNotEmpty())
            return cmd.trim();

        const auto configFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                  .getChildFile ("Sattari").getChildFile ("StemDeck")
                                  .getChildFile ("StemProvider").getChildFile ("transcriber-command.txt");
        if (configFile.existsAsFile())
        {
            cmd = configFile.loadFileAsString().trim();
            if (cmd.isNotEmpty())
                return cmd;
        }

        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
        const juce::StringArray candidates {
            home + "/.local/share/stemdeck-tools/bin/basic-pitch",   // StemDeck's own venv
            home + "/Library/Python/3.9/bin/basic-pitch",
            home + "/Library/Python/3.10/bin/basic-pitch",
            home + "/Library/Python/3.11/bin/basic-pitch",
            home + "/Library/Python/3.12/bin/basic-pitch",
            "/usr/local/bin/basic-pitch",
            "/opt/homebrew/bin/basic-pitch",
        };
        for (const auto& path : candidates)
            if (juce::File (path).existsAsFile())
                return path;

        juce::ChildProcess which;
        if (which.start (juce::StringArray { "which", "basic-pitch" }))
        {
            const auto path = which.readAllProcessOutput().trim().upToFirstOccurrenceOf ("\n", false, false).trim();
            if (path.isNotEmpty() && juce::File (path).existsAsFile())
                return path;
        }
        return {};
    }

    inline bool isAvailable() { return detectCommand().isNotEmpty(); }

    // Run basic-pitch on `input` and parse the resulting MIDI file.
    // BLOCKING — call from a background thread only. Returns true on success.
    inline bool transcribe (const juce::File& input, std::vector<MelodyExtractor::Note>& outNotes,
                            juce::String& message)
    {
        const auto command = detectCommand();
        if (command.isEmpty())
        {
            message = "basic-pitch not installed";
            return false;
        }

        auto outDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("stemdeck-transcribe-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        outDir.createDirectory();

        juce::ChildProcess proc;
        if (! proc.start (juce::StringArray { command, outDir.getFullPathName(), input.getFullPathName() }))
        {
            message = "could not start basic-pitch";
            outDir.deleteRecursively();
            return false;
        }

        juce::String output;
        const juce::uint32 start = juce::Time::getMillisecondCounter();
        constexpr juce::uint32 timeoutMs = 10u * 60u * 1000u;
        while (proc.isRunning())
        {
            output += proc.readAllProcessOutput();
            if (juce::Time::getMillisecondCounter() - start > timeoutMs)
            {
                proc.kill();
                message = "basic-pitch timed out";
                outDir.deleteRecursively();
                return false;
            }
            juce::Thread::sleep (150);
        }
        output += proc.readAllProcessOutput();

        // Find the produced .mid (named <input-stem>_basic_pitch.mid).
        juce::Array<juce::File> mids;
        outDir.findChildFiles (mids, juce::File::findFiles, true, "*.mid");
        if (mids.isEmpty())
        {
            message = "basic-pitch produced no MIDI (exit " + juce::String ((int64) proc.getExitCode())
                    + "): " + output.trim().substring (0, 160);
            outDir.deleteRecursively();
            return false;
        }

        juce::FileInputStream stream (mids[0]);
        juce::MidiFile midi;
        if (! stream.openedOk() || ! midi.readFrom (stream))
        {
            message = "could not parse basic-pitch MIDI";
            outDir.deleteRecursively();
            return false;
        }
        midi.convertTimestampTicksToSeconds();

        outNotes.clear();
        for (int t = 0; t < midi.getNumTracks(); ++t)
        {
            const auto* track = midi.getTrack (t);
            for (int e = 0; e < track->getNumEvents(); ++e)
            {
                const auto& msg = track->getEventPointer (e)->message;
                if (! msg.isNoteOn())
                    continue;
                const auto* off = track->getEventPointer (e)->noteOffObject;
                const double onT  = msg.getTimeStamp();
                const double offT = off != nullptr ? off->message.getTimeStamp() : onT + 0.25;
                MelodyExtractor::Note n;
                n.pitch         = msg.getNoteNumber();
                n.startSeconds  = onT;
                n.lengthSeconds = juce::jmax (0.05, offT - onT);
                n.velocity      = juce::jlimit (1, 127, (int) msg.getVelocity());
                outNotes.push_back (n);
            }
        }
        outDir.deleteRecursively();

        std::sort (outNotes.begin(), outNotes.end(),
                   [] (const auto& a, const auto& b) { return a.startSeconds < b.startSeconds; });
        message = "neural transcription: " + juce::String ((int) outNotes.size()) + " notes";
        return ! outNotes.empty();
    }
}
