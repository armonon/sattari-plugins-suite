#pragma once

// =============================================================================
//  Sattari Audio Suite — shared preset manager
// -----------------------------------------------------------------------------
//  One preset system for the whole suite. Backed by each plugin's APVTS, presets
//  are stored as portable, human-diffable JSON (paramID -> real value) under a
//  shared suite folder so the Hub (and future cloud sync) can see every plugin's
//  presets in one place:
//
//     ~/Library/Application Support/Sattari Audio/Presets/<productId>/<name>.sattaripreset
//
//  Portability: because presets are a flat {id: value} map (not a raw ValueTree
//  dump), they survive parameters being added/removed between versions — unknown
//  ids are ignored, missing ids keep their current value.
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <utility>
#include <vector>

namespace sattari::presets
{

class SattariPresetManager
{
public:
    SattariPresetManager (juce::AudioProcessorValueTreeState& stateToManage,
                          juce::String productId,
                          juce::String suiteFolderName = "Sattari Audio")
        : apvts (stateToManage),
          product (std::move (productId)),
          suiteFolder (std::move (suiteFolderName))
    {
    }

    static constexpr const char* fileExtension = ".sattaripreset";

    juce::File presetDirectory() const
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile (suiteFolder)
                       .getChildFile ("Presets")
                       .getChildFile (product);
        dir.createDirectory();
        return dir;
    }

    // --- Capture / apply (also reusable for getStateInformation if desired) ---
    juce::var captureState() const
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("schemaVersion", 1);
        object->setProperty ("product", product);

        auto* values = new juce::DynamicObject();
        for (auto* p : apvts.processor.getParameters())
        {
            auto* withId  = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
            auto* ranged  = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (withId == nullptr || ranged == nullptr)
                continue;
            values->setProperty (withId->paramID, (double) ranged->convertFrom0to1 (ranged->getValue()));
        }
        object->setProperty ("parameters", juce::var (values));
        return juce::var (object);
    }

    void applyState (const juce::var& state)
    {
        const auto params = state.getProperty ("parameters", juce::var());
        auto* values = params.getDynamicObject();
        if (values == nullptr)
            return;

        for (auto* p : apvts.processor.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (withId == nullptr || ranged == nullptr)
                continue;
            if (! values->hasProperty (withId->paramID))
                continue;

            const float real = (float) (double) values->getProperty (withId->paramID);
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (real));
            ranged->endChangeGesture();
        }
    }

    // --- Factory presets ------------------------------------------------------
    //  Registered in-memory by each plugin in its editor/processor ctor as a list
    //  of (paramID -> real value) pairs. Only listed params are set; everything
    //  else keeps its current value, so a preset stays valid as params evolve.
    using PresetValues = std::vector<std::pair<juce::String, float>>;

    void addFactoryPreset (const juce::String& name, PresetValues values)
    {
        factoryPresets.push_back ({ name, std::move (values) });
    }

    juce::StringArray factoryPresetNames() const
    {
        juce::StringArray names;
        for (const auto& fp : factoryPresets)
            names.add (fp.name);
        return names;
    }

    bool loadFactoryPreset (const juce::String& name)
    {
        for (const auto& fp : factoryPresets)
            if (fp.name == name)
            {
                applyValues (fp.values);
                return true;
            }
        return false;
    }

    bool loadFactoryPreset (int index)
    {
        if (index < 0 || index >= (int) factoryPresets.size())
            return false;
        applyValues (factoryPresets[(size_t) index].values);
        return true;
    }

    int numFactoryPresets() const noexcept { return (int) factoryPresets.size(); }

    // Set only the named parameters, notifying the host (undo/automation aware).
    void applyValues (const PresetValues& values)
    {
        for (const auto& idValue : values)
            if (auto* ranged = apvts.getParameter (idValue.first))
            {
                ranged->beginChangeGesture();
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (idValue.second));
                ranged->endChangeGesture();
            }
    }

    // --- User preset files ----------------------------------------------------
    bool saveUserPreset (const juce::String& name)
    {
        const auto file = fileForName (name);
        return file.replaceWithText (juce::JSON::toString (captureState()));
    }

    bool loadUserPreset (const juce::String& name)
    {
        return loadFile (fileForName (name));
    }

    bool loadFile (const juce::File& file)
    {
        if (! file.existsAsFile())
            return false;
        const auto parsed = juce::JSON::parse (file.loadFileAsString());
        if (! parsed.isObject())
            return false;
        applyState (parsed);
        return true;
    }

    bool deleteUserPreset (const juce::String& name)
    {
        return fileForName (name).deleteFile();
    }

    juce::StringArray userPresetNames() const
    {
        juce::StringArray names;
        for (const auto& f : presetDirectory().findChildFiles (juce::File::findFiles, false,
                                                               juce::String ("*") + fileExtension))
            names.add (f.getFileNameWithoutExtension());
        names.sortNatural();
        return names;
    }

private:
    juce::File fileForName (const juce::String& name) const
    {
        return presetDirectory().getChildFile (juce::File::createLegalFileName (name) + fileExtension);
    }

    struct FactoryPreset { juce::String name; PresetValues values; };

    juce::AudioProcessorValueTreeState& apvts;
    juce::String product;
    juce::String suiteFolder;
    std::vector<FactoryPreset> factoryPresets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariPresetManager)
};

} // namespace sattari::presets
