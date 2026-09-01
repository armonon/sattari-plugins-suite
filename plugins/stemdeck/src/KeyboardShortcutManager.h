#pragma once

#include <JuceHeader.h>

#include <map>
#include <vector>

namespace stemdeck
{
/** User keyboard bindings are deliberately separate from MIDI mappings and
    project state. They describe this workstation, not a particular set. */
class KeyboardShortcutManager
{
public:
    enum class Scope { global, perform, replay };

    struct DefaultBinding
    {
        int commandId = 0;
        Scope scope = Scope::global;
        juce::KeyPress key;
    };

    explicit KeyboardShortcutManager (
        std::vector<DefaultBinding> defaultBindings = stemDeckDefaults(),
        juce::File settings = defaultSettingsFile())
        : settingsFile (std::move (settings))
    {
        for (const auto& binding : defaultBindings)
            if (binding.commandId > 0)
            {
                scopes[binding.commandId] = binding.scope;
                if (binding.key.isValid())
                    defaults[binding.commandId] = binding.key;
            }

        // Commands without factory shortcuts still need a registered scope so a
        // newly assigned key is serialised and restored on the next launch.
        for (int commandId = 1; commandId <= 67; ++commandId)
            scopes.try_emplace (commandId, Scope::global);

        bindings = defaults;
        load();
    }

    static juce::File defaultSettingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Sattari")
            .getChildFile ("StemDeck")
            .getChildFile ("keyboard-shortcuts.json");
    }

    static std::vector<DefaultBinding> stemDeckDefaults()
    {
        using K = juce::KeyPress;
        using M = juce::ModifierKeys;
        const auto key = [] (int code, int mods = 0)
        {
            return K (code, M (mods), (juce_wchar) code);
        };

        std::vector<DefaultBinding> result {
            { 1, Scope::global, key ('1', M::commandModifier) },
            { 2, Scope::global, key ('2', M::commandModifier) },
            { 4, Scope::global, key ('3', M::commandModifier) },
            { 3, Scope::global, key ('4', M::commandModifier) },
            { 5, Scope::global, key ('O', M::commandModifier) },
            { 10, Scope::global, key ('L') },
            { 12, Scope::global, key ('F') },
            { 13, Scope::global, key ('B') },
            { 14, Scope::global, key ('K') },
            { 15, Scope::global, key ('R') },
            { 16, Scope::global, key ('S', M::commandModifier) },
            { 17, Scope::global, key ('O', M::commandModifier | M::shiftModifier) },
            { 18, Scope::global, key ('I', M::commandModifier) },
            { 21, Scope::global, key ('S', M::commandModifier | M::shiftModifier) },
            { 28, Scope::global, key ('Z', M::commandModifier) },
            { 29, Scope::global, key ('Z', M::commandModifier | M::shiftModifier) },
            { 33, Scope::perform, K (K::tabKey) },
            { 34, Scope::perform, K (K::spaceKey) },
            { 35, Scope::perform, K (K::spaceKey, M (M::shiftModifier), ' ') },
            { 49, Scope::replay, key ('D', M::commandModifier) },
            { 50, Scope::replay, key ('0') },
            { 51, Scope::replay, K (K::spaceKey) },
            { 52, Scope::replay, key ('A') },
            { 53, Scope::replay, key ('B') },
            { 54, Scope::replay, key ('F') },
            { 55, Scope::replay, key ('0', M::commandModifier) }
            ,{ 56, Scope::global, key ('K', M::commandModifier) }
        };

        for (int pad = 0; pad < 8; ++pad)
            result.push_back ({ 101 + pad, Scope::perform, key ('1' + pad) });
        for (int deck = 0; deck < 4; ++deck)
            for (int action = 0; action < 11; ++action)
                result.push_back ({ 200 + deck * 16 + action, Scope::perform, {} });
        return result;
    }

    int commandFor (const juce::KeyPress& key, Scope activeScope) const
    {
        // Contextual commands take priority over globals. This keeps familiar
        // one-key controls useful: B can be razor in Replay and sync in Perform.
        for (const auto& [id, binding] : bindings)
            if (binding == key && scopeFor (id) == activeScope)
                return id;
        for (const auto& [id, binding] : bindings)
            if (binding == key && scopeFor (id) == Scope::global)
                return id;
        return 0;
    }

    juce::KeyPress keyFor (int commandId) const
    {
        const auto found = bindings.find (commandId);
        return found != bindings.end() ? found->second : juce::KeyPress();
    }

    juce::String descriptionFor (int commandId, bool icons = true) const
    {
        const auto key = keyFor (commandId);
        return key.isValid() ? (icons ? key.getTextDescriptionWithIcons()
                                      : key.getTextDescription())
                             : juce::String();
    }

    Scope scopeFor (int commandId) const
    {
        const auto found = scopes.find (commandId);
        return found != scopes.end() ? found->second : Scope::global;
    }

    int conflictFor (int commandId, const juce::KeyPress& key) const
    {
        if (! key.isValid()) return 0;
        const auto wantedScope = scopeFor (commandId);
        for (const auto& [id, binding] : bindings)
        {
            if (id == commandId || binding != key) continue;
            const auto existingScope = scopeFor (id);
            if (wantedScope == existingScope)
                return id;
        }
        return 0;
    }

    bool assign (int commandId, const juce::KeyPress& key, bool replaceConflict)
    {
        if (commandId <= 0 || ! key.isValid() || isReservedBySystem (key))
            return false;
        const int conflict = conflictFor (commandId, key);
        if (conflict != 0 && ! replaceConflict)
            return false;
        if (conflict != 0)
            bindings.erase (conflict);
        bindings[commandId] = key;
        save();
        return true;
    }

    void clear (int commandId)
    {
        bindings.erase (commandId);
        save();
    }

    void reset (int commandId)
    {
        if (const auto found = defaults.find (commandId); found != defaults.end())
            bindings[commandId] = found->second;
        else
            bindings.erase (commandId);
        save();
    }

    void resetAll()
    {
        bindings = defaults;
        save();
    }

    static bool isReservedBySystem (const juce::KeyPress& key)
    {
       #if JUCE_MAC
        if (! key.getModifiers().isCommandDown()
            || key.getModifiers().isCtrlDown()
            || key.getModifiers().isAltDown())
            return false;
        const int code = key.getKeyCode();
        return code == 'Q' || code == 'H' || code == 'M' || code == 'W';
       #else
        return false;
       #endif
    }

    bool save() const
    {
        if (settingsFile == juce::File()) return true;
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty ("version", 1);
        juce::Array<juce::var> stored;

        for (const auto& [id, scope] : scopes)
        {
            juce::ignoreUnused (scope);
            const auto current = bindings.find (id);
            const auto original = defaults.find (id);
            if (current == bindings.end() && original == defaults.end())
                continue;
            const bool unchanged = current != bindings.end() && original != defaults.end()
                                && current->second == original->second;
            if (unchanged) continue;

            auto item = std::make_unique<juce::DynamicObject>();
            item->setProperty ("command", id);
            item->setProperty ("key", current != bindings.end()
                                      ? current->second.getTextDescription()
                                      : juce::String());
            stored.add (juce::var (item.release()));
        }
        root->setProperty ("bindings", stored);

        settingsFile.getParentDirectory().createDirectory();
        juce::TemporaryFile temporary (settingsFile);
        if (! temporary.getFile().replaceWithText (
                juce::JSON::toString (juce::var (root.release()), true)))
            return false;
        return temporary.overwriteTargetFileWithTemporary();
    }

private:
    void load()
    {
        if (! settingsFile.existsAsFile()) return;
        const auto parsed = juce::JSON::parse (settingsFile.loadFileAsString());
        const auto* root = parsed.getDynamicObject();
        if (root == nullptr) return;
        const auto* items = root->getProperty ("bindings").getArray();
        if (items == nullptr) return;

        for (const auto& value : *items)
        {
            const auto* item = value.getDynamicObject();
            if (item == nullptr) continue;
            const int id = (int) item->getProperty ("command");
            if (scopes.find (id) == scopes.end()) continue;
            const auto description = item->getProperty ("key").toString();
            if (description.isEmpty())
            {
                bindings.erase (id);
                continue;
            }
            const auto key = juce::KeyPress::createFromDescription (description);
            if (key.isValid() && ! isReservedBySystem (key))
                bindings[id] = key;
        }

        // Reject stale or manually edited conflicts deterministically. Earlier
        // entries win, making a corrupt preferences file harmless at launch.
        for (auto left = bindings.begin(); left != bindings.end(); ++left)
            for (auto right = std::next (left); right != bindings.end();)
                if (left->second == right->second
                    && scopeFor (left->first) == scopeFor (right->first))
                    right = bindings.erase (right);
                else
                    ++right;
    }

    juce::File settingsFile;
    std::map<int, juce::KeyPress> defaults, bindings;
    std::map<int, Scope> scopes;
};
} // namespace stemdeck
