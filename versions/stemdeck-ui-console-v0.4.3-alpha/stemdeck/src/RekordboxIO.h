#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>
#include "MusicLibraryView.h"

// =============================================================================
//  RekordboxIO — rekordbox.xml interop (the practical cross-ecosystem bridge).
// -----------------------------------------------------------------------------
//  Export: DJ_PLAYLISTS/COLLECTION with TRACK entries — Location (file URI),
//  AverageBpm, Tonality — plus POSITION_MARK children carrying StemDeck's hot
//  cues. Readable by rekordbox, and by Serato/Traktor/djay via their importers.
//
//  Import: parses any rekordbox.xml, returning path → cue list so StemDeck can
//  apply cue points whenever a matching file is loaded onto a deck.
// =============================================================================
namespace RekordboxIO
{
    struct Cue { int num = 0; double seconds = 0.0; };
    using CueMap = std::map<juce::String, std::vector<Cue>>;   // full path → cues

    inline juce::String tonalityName (int pitchClass, bool minor)
    {
        static const char* names[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        if (pitchClass < 0 || pitchClass > 11) return {};
        return juce::String (names[pitchClass]) + (minor ? "m" : "");
    }

    // cueProvider: given a full file path, fill cues (num, seconds) if known.
    inline bool exportXml (const juce::File& dest,
                           const std::vector<MusicLibraryView::ExportEntry>& tracks,
                           const std::function<void (const juce::String&, std::vector<Cue>&)>& cueProvider)
    {
        juce::XmlElement root ("DJ_PLAYLISTS");
        root.setAttribute ("Version", "1.0.0");

        auto* product = root.createNewChildElement ("PRODUCT");
        product->setAttribute ("Name", "Sattari StemDeck");
        product->setAttribute ("Version", "1.0");
        product->setAttribute ("Company", "Sattari");

        auto* collection = root.createNewChildElement ("COLLECTION");
        collection->setAttribute ("Entries", (int) tracks.size());

        int trackId = 1;
        for (const auto& t : tracks)
        {
            auto* tr = collection->createNewChildElement ("TRACK");
            tr->setAttribute ("TrackID", trackId++);
            tr->setAttribute ("Name", t.file.getFileNameWithoutExtension());
            tr->setAttribute ("Location", juce::URL (t.file).toString (false));
            if (t.bpm > 0.0f)
                tr->setAttribute ("AverageBpm", juce::String (t.bpm, 2));
            const auto tonality = tonalityName (t.keyPitchClass, t.keyMinor);
            if (tonality.isNotEmpty())
                tr->setAttribute ("Tonality", tonality);

            if (cueProvider != nullptr)
            {
                std::vector<Cue> cues;
                cueProvider (t.file.getFullPathName(), cues);
                for (const auto& c : cues)
                {
                    auto* pm = tr->createNewChildElement ("POSITION_MARK");
                    pm->setAttribute ("Name", "");
                    pm->setAttribute ("Type", 0);
                    pm->setAttribute ("Start", juce::String (c.seconds, 3));
                    pm->setAttribute ("Num", c.num);
                }
            }
        }

        root.createNewChildElement ("PLAYLISTS");   // empty node keeps importers happy
        return root.writeTo (dest, {});
    }

    inline CueMap importXml (const juce::File& source, int* tracksSeen = nullptr)
    {
        CueMap map;
        const auto xml = juce::XmlDocument::parse (source);
        if (xml == nullptr)
            return map;

        int seen = 0;
        if (auto* collection = xml->getChildByName ("COLLECTION"))
        {
            for (auto* tr : collection->getChildWithTagNameIterator ("TRACK"))
            {
                ++seen;
                const auto location = tr->getStringAttribute ("Location");
                if (location.isEmpty())
                    continue;
                const auto path = juce::URL (location).getLocalFile().getFullPathName();
                if (path.isEmpty())
                    continue;

                std::vector<Cue> cues;
                for (auto* pm : tr->getChildWithTagNameIterator ("POSITION_MARK"))
                {
                    Cue c;
                    c.num     = pm->getIntAttribute ("Num", 0);
                    c.seconds = pm->getDoubleAttribute ("Start", -1.0);
                    if (c.seconds >= 0.0 && c.num >= 0 && c.num < 8)
                        cues.push_back (c);
                }
                if (! cues.empty())
                    map[path] = std::move (cues);
            }
        }
        if (tracksSeen != nullptr)
            *tracksSeen = seen;
        return map;
    }

    // ── Persistence of imported cues (applied whenever a deck loads a match) ─
    inline juce::File cueStoreFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Sattari").getChildFile ("StemDeck")
                 .getChildFile ("rekordbox-cues.json");
    }

    inline void saveCueStore (const CueMap& map)
    {
        auto* obj = new juce::DynamicObject();
        for (const auto& [path, cues] : map)
        {
            juce::Array<juce::var> arr;
            for (const auto& c : cues)
            {
                auto* cue = new juce::DynamicObject();
                cue->setProperty ("num", c.num);
                cue->setProperty ("sec", c.seconds);
                arr.add (juce::var (cue));
            }
            obj->setProperty (path, arr);
        }
        cueStoreFile().getParentDirectory().createDirectory();
        cueStoreFile().replaceWithText (juce::JSON::toString (juce::var (obj), true));
    }

    inline CueMap loadCueStore()
    {
        CueMap map;
        const auto parsed = juce::JSON::parse (cueStoreFile());
        if (auto* obj = parsed.getDynamicObject())
        {
            for (const auto& prop : obj->getProperties())
            {
                std::vector<Cue> cues;
                if (const auto* arr = prop.value.getArray())
                    for (const auto& v : *arr)
                        if (auto* cueObj = v.getDynamicObject())
                            cues.push_back ({ (int) cueObj->getProperty ("num"),
                                              (double) cueObj->getProperty ("sec") });
                if (! cues.empty())
                    map[prop.name.toString()] = std::move (cues);
            }
        }
        return map;
    }
}
