#pragma once

// =============================================================================
//  SessionExport — write a rendered session to deliverables: a mastered WAV, a
//  DJ tracklist, and a .cue sheet (brief §6 export targets).
// -----------------------------------------------------------------------------
//  Renders via SessionRenderer, then serialises. The tracklist/cue timestamps
//  come straight from the deckLoad events converted to wall-clock via the master
//  tempo, so they re-time with the session like everything else.
// =============================================================================

#include "SessionRenderer.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace stemdeck::session
{

class SessionExport
{
public:
    // Render the session and write it as an N-bit WAV (24 by default — effectively
    // transparent). Returns false on any failure.
    static bool renderToWav (const SessionModel& s, const SessionRenderer::AudioProvider& provider,
                             const juce::File& out, int bitDepth = 24)
    {
        const auto buf = SessionRenderer::render (s, provider);
        return writeWav (buf, s.sampleRate, out, bitDepth);
    }

    static bool writeWav (const juce::AudioBuffer<float>& buf, double sr, const juce::File& out, int bitDepth = 24)
    {
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());
        if (os == nullptr) return false;
        std::unique_ptr<juce::AudioFormatWriter> w (
            wav.createWriterFor (os.get(), sr, (unsigned) juce::jmax (1, buf.getNumChannels()),
                                 bitDepth, {}, 0));
        if (w == nullptr) return false;
        os.release();   // writer owns the stream now
        return w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }

    // "mm:ss  Track name" per deckLoad, in mix order.
    static juce::String tracklist (const SessionModel& s)
    {
        juce::String out;
        for (const auto& e : s.events)
            if (e.type == EventType::deckLoad)
                out << timeMMSS (secondsAt (s, e.atBeat)) << "  " << trackName (s, e.trackId) << juce::newLine;
        return out;
    }

    // Standard .cue sheet referencing wavName (MM:SS:FF frames).
    static juce::String cueSheet (const SessionModel& s, const juce::String& wavName)
    {
        juce::String out;
        out << "FILE \"" << wavName << "\" WAVE" << juce::newLine;
        int idx = 1;
        for (const auto& e : s.events)
            if (e.type == EventType::deckLoad)
            {
                out << "  TRACK " << juce::String (idx).paddedLeft ('0', 2) << " AUDIO" << juce::newLine
                    << "    TITLE \"" << trackName (s, e.trackId) << "\"" << juce::newLine
                    << "    INDEX 01 " << timeCue (secondsAt (s, e.atBeat)) << juce::newLine;
                ++idx;
            }
        return out;
    }

private:
    static double secondsAt (const SessionModel& s, double beat)
    {
        return s.sampleRate > 0.0 ? s.sampleTimeForBeat (beat) / s.sampleRate : 0.0;
    }
    static juce::String trackName (const SessionModel& s, const juce::String& id)
    {
        for (const auto& t : s.tracks)
            if (t.id == id)
            {
                const auto name = juce::File (t.path).getFileName();
                return name.isNotEmpty() ? name : id;
            }
        return id;
    }
    static juce::String timeMMSS (double sec)
    {
        const int m = (int) (sec / 60.0), ss = ((int) sec) % 60;
        return juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (ss).paddedLeft ('0', 2);
    }
    static juce::String timeCue (double sec)   // MM:SS:FF (75 frames/sec)
    {
        const int m = (int) (sec / 60.0), ss = ((int) sec) % 60;
        const int ff = (int) ((sec - std::floor (sec)) * 75.0);
        return juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (ss).paddedLeft ('0', 2)
             + ":" + juce::String (ff).paddedLeft ('0', 2);
    }
};

} // namespace stemdeck::session
