#pragma once

#include <juce_core/juce_core.h>

namespace stemdeck
{

// Small, UI-independent selection model for the live insert router. Chain identity,
// not list position or deck focus, is the source of truth.
class FxTargetModel
{
public:
    enum class Kind { master = 0, channel, stem };

    void setTargets (juce::StringArray newNames, juce::Array<int> newChains,
                     juce::Array<int> newKinds)
    {
        const bool hadTargets = ! chains.isEmpty();
        const int previousChain = currentChain();
        names = std::move (newNames);
        chains = std::move (newChains);
        kinds = std::move (newKinds);
        selected = 0;
        if (hadTargets)
            for (int i = 0; i < chains.size(); ++i)
                if (chains[i] == previousChain) { selected = i; break; }
    }

    bool selectChain (int chain)
    {
        for (int i = 0; i < chains.size(); ++i)
            if (chains[i] == chain) { selected = i; return true; }
        return false;
    }

    int currentChain() const noexcept
    {
        return selected >= 0 && selected < chains.size() ? chains[selected] : 0;
    }

    Kind currentKind() const noexcept
    {
        const int kind = selected >= 0 && selected < kinds.size() ? kinds[selected] : 0;
        return (Kind) juce::jlimit (0, 2, kind);
    }

    juce::String currentName() const
    {
        return selected >= 0 && selected < names.size() ? names[selected]
                                                         : juce::String ("Master Bus");
    }

    int currentIndex() const noexcept { return selected; }

private:
    juce::StringArray names;
    juce::Array<int> chains, kinds;
    int selected = 0;
};

} // namespace stemdeck
