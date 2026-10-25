#pragma once

#include <juce_core/juce_core.h>

// A component-level fixture seam, not a sandbox for StemDeck or third-party code.
// The caller supplies and owns an existing directory. No fallback to user paths
// is permitted in restricted mode. Keep the fixture free of concurrent writers:
// path checks cannot prevent another process changing a directory after a check.
class ComponentStoragePolicy
{
public:
    static ComponentStoragePolicy production() { return {}; }

    static ComponentStoragePolicy restrictedFixture (const juce::File& root)
    {
        ComponentStoragePolicy policy;
        policy.restricted = true;
        policy.fixtureRoot = root;
        return policy;
    }

    bool isRestricted() const noexcept { return restricted; }

    bool isValid() const
    {
        if (! restricted)
            return true;
        if (fixtureRoot == juce::File() || fixtureRoot.getParentDirectory() == fixtureRoot)
            return false;
        // Check symlinks before following them with isDirectory(). This also
        // rejects an alias anywhere in the supplied root's ancestor chain.
        for (auto node = fixtureRoot;; node = node.getParentDirectory())
        {
            if (node.isSymbolicLink())
                return false;
            if (node.getParentDirectory() == node)
                break;
        }
        return fixtureRoot.isDirectory();
    }

    bool permitsPath (const juce::File& path) const
    {
        if (! restricted)
            return true;
        if (! isValid() || path == juce::File()
            || (path != fixtureRoot && ! path.isAChildOf (fixtureRoot)))
            return false;
        for (auto node = path; node != fixtureRoot; node = node.getParentDirectory())
            if (node.isSymbolicLink())
                return false;
        return true;
    }

    juce::File root() const { return isValid() ? fixtureRoot : juce::File(); }

private:
    bool restricted = false;
    juce::File fixtureRoot;
};
