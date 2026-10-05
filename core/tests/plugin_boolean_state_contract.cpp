#include "boolean_state_contract.h"
#include <memory>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    if (! processor) return 1;
    return runBooleanStateContract (*processor) == 0 ? 0 : 1;
}
