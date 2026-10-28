#include "HostGraph.h"
#include "StemProvider.h"
#include <iostream>

int main (int argc, char** argv)
{
    if (argc != 2)
        return 2;
    const juce::File base (juce::String::fromUTF8 (argv[1]));
    if (! base.isDirectory())
        return 2;
    juce::ScopedJuceInitialiser_GUI initialise;
    int failures = 0;
    auto check = [&] (bool value, const char* name)
    {
        std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
        failures += ! value;
    };
    const auto root = base.getChildFile ("component-root");
    const auto outside = base.getChildFile ("component-outside");
    if (root.exists() || outside.exists())
        return 2;
    if (root.createDirectory().failed() || outside.createDirectory().failed())
        return 2;
    const auto policy = ComponentStoragePolicy::restrictedFixture (root);
    HostGraph graph (policy);
    check (graph.getSupportedPluginFormats().isEmpty(), "no external plugin formats registered");
    check (graph.getCacheFile().isAChildOf (root), "host metadata root injected");
    check (graph.scanDefaultLocations().scannedFormats.isEmpty(), "external scan rejected");
    bool rejectedLoad = false;
    graph.loadPluginToChainAsync (HostGraph::Chain::master, {}, [&] (bool ok, juce::String message)
    {
        rejectedLoad = ! ok && message.contains ("disabled");
    });
    check (rejectedLoad, "external plugin load rejected");
    check (graph.saveCache(), "fixture metadata save allowed");
    check (graph.loadCache(), "fixture metadata reload allowed");
    HostGraph invalidGraph (ComponentStoragePolicy::restrictedFixture ({}));
    check (invalidGraph.getCacheFile() == juce::File() && ! invalidGraph.loadCache()
           && ! invalidGraph.saveCache(), "invalid host root has no fallback");

    StemProvider provider (policy);
    check (provider.isAvailable() && provider.getCacheRoot().isAChildOf (root), "stem cache injected at construction");
    check (provider.getSeparatorCommand().isEmpty() && ! provider.neuralAvailable(), "backend discovery disabled");
    provider.setSeparatorCommand ("must-not-run {input} {output}");
    check (provider.getSeparatorCommand().isEmpty(), "separator setter cannot grant capability");
    juce::String message;
    check (! provider.runSeparatorCommand ("must-not-run", outside.getChildFile ("source"), outside, message)
           && message.contains ("disabled"), "direct separator entry rejected");
    check (! provider.runNeuralSeparation (outside.getChildFile ("source"), outside, message)
           && message.contains ("disabled"), "direct neural entry rejected");
    bool rejectedJob = false;
    provider.requestStemsAsync (outside.getChildFile ("source"), [&] (StemProvider::Result result)
    {
        rejectedJob = result.message.contains ("disabled") && ! result.allCoreStemsReady;
    });
    check (rejectedJob, "separation queue rejected before file access");
    check (provider.inspectCache (outside.getChildFile ("source")).cacheDirectory == juce::File(), "external cache inspection rejected");
    check (provider.getStatusForSource (outside.getChildFile ("source")).headline.contains ("rejected"), "external status inspection rejected");
    const auto source = root.getChildFile ("source.wav");
    if (! source.replaceWithText ("No audio decoding in this cache contract."))
        return 2;
    check (provider.inspectCache (source).stems[DeckPlayer::fullMixStem].ready, "local source cache inspection allowed");
    provider.setCacheRoot (outside);
    check (! provider.isAvailable() && provider.getCacheRoot() == juce::File()
           && provider.getCacheDirectoryForSource (source) == juce::File(), "cache setter fails closed outside root");
    StemProvider invalidProvider (ComponentStoragePolicy::restrictedFixture ({}));
    check (! invalidProvider.isAvailable() && invalidProvider.getCacheRoot() == juce::File(), "invalid stem root has no fallback");
    return failures == 0 ? 0 : 1;
}
