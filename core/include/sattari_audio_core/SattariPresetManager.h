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
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace sattari::presets
{

class SattariPresetManager
{
public:
    SattariPresetManager (juce::AudioProcessorValueTreeState& stateToManage,
                          juce::String productId,
                          juce::String suiteFolderName = "Sattari Audio",
                          juce::File presetRoot = {})
        : apvts (stateToManage),
          product (std::move (productId)),
          suiteFolder (std::move (suiteFolderName)),
          directoryOverride (std::move (presetRoot))
    {
    }

    static constexpr const char* fileExtension = ".sattaripreset";

    juce::File presetDirectory() const
    {
        auto dir = directoryOverride != juce::File() ? directoryOverride
                   : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
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

    bool applyState (const juce::var& state)
    {
        const auto schema = state.getProperty ("schemaVersion", juce::var());
        if (! state.isObject() || ! schema.isInt() || (int) schema != 1
            || state.getProperty ("product", juce::var()) != juce::var (product))
            return false;
        const auto params = state.getProperty ("parameters", juce::var());
        auto* values = params.getDynamicObject();
        if (values == nullptr || values->getProperties().size() == 0)
            return false;

        // Validate the whole file before notifying the host about ANY change.
        // JUCE var casts otherwise silently coerce strings/null to zero and may
        // propagate infinity through a parameter's normalisation function.
        for (const auto& entry : values->getProperties())
        {
            const auto& value = entry.value;
            if (! (value.isInt() || value.isInt64() || value.isDouble()))
                return false;
            const double real = (double) value;
            if (! std::isfinite (real) || std::abs (real) > std::numeric_limits<float>::max())
                return false;
        }

        bool recognised = false;
        for (auto* p : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                recognised = recognised || values->hasProperty (ranged->paramID);
        if (! recognised) return false;

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
            const auto& range = ranged->getNormalisableRange();
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (juce::jlimit (range.start, range.end, real)));
            ranged->endChangeGesture();
        }
        return true;
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

    // --- A/B compare ----------------------------------------------------------
    //  Two in-memory slots for snapshot-and-recall. Editors give the user three
    //  buttons (A, B, Swap) and the plugin can flip between two states without
    //  touching the file-backed user presets. Slots are NOT persisted across
    //  reloads (that is what factory + user presets are for).
    void captureSlotA() { slotAState = captureState(); slotAHasValue = true; }
    void captureSlotB() { slotBState = captureState(); slotBHasValue = true; }
    bool recallSlotA()  { return slotAHasValue && applyState (slotAState); }
    bool recallSlotB()  { return slotBHasValue && applyState (slotBState); }
    void swapSlots()
    {
        std::swap (slotAState, slotBState);
        std::swap (slotAHasValue, slotBHasValue);
    }
    bool hasSlotA() const noexcept { return slotAHasValue; }
    bool hasSlotB() const noexcept { return slotBHasValue; }

    // --- User preset files ----------------------------------------------------
    juce::String newUserPresetName() const
    {
        // Rapid saves must not silently overwrite a preset made in this second.
        return presetDirectory().getNonexistentChildFile (
            "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"),
            fileExtension, false).getFileNameWithoutExtension();
    }

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
        constexpr int maxPresetBytes = 1024 * 1024;
        if (! file.existsAsFile() || file.getSize() > maxPresetBytes)
            return false;
        juce::FileInputStream input (file);
        if (! input.openedOk()) return false;
        juce::MemoryBlock bytes;
        input.readIntoMemoryBlock (bytes, maxPresetBytes + 1);
        if (input.getStatus().failed() || bytes.getSize() > (size_t) maxPresetBytes)
            return false;
        // JUCE's JSON parser is recursive. Presets have only two object levels;
        // bound nesting before parsing and reject trailing/truncated documents.
        int depth = 0;
        bool quoted = false, escaped = false, started = false;
        for (size_t i = 0; i < bytes.getSize(); ++i)
        {
            const char c = static_cast<const char*> (bytes.getData())[i];
            if (c == 0) return false;
            if (quoted)
            {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
                continue;
            }
            if (depth == 0)
            {
                if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
                if (started || c != '{') return false;
                started = true;
            }
            if (c == '"') quoted = true;
            else if (c == '{' || c == '[') { if (++depth > 16) return false; }
            else if (c == '}' || c == ']') { if (--depth < 0) return false; }
        }
        if (! started || quoted || depth != 0
            || ! juce::CharPointer_UTF8::isValidString (
                static_cast<const char*> (bytes.getData()), (int) bytes.getSize())) return false;
        return applyState (juce::JSON::parse (juce::String::fromUTF8 (
            static_cast<const char*> (bytes.getData()), (int) bytes.getSize())));
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
    juce::File directoryOverride;
    std::vector<FactoryPreset> factoryPresets;

    // A/B slots (in-memory).
    juce::var slotAState, slotBState;
    bool slotAHasValue = false, slotBHasValue = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariPresetManager)
};

} // namespace sattari::presets
