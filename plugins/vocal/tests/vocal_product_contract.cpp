#include "../src/PluginProcessor.h"
#include <rack_product_contract.h>

int main()
{
    return sattari::test::runRackProductContract<SattariVocalAudioProcessor> (
        "Sattari Vocal", 10, 10);
}
