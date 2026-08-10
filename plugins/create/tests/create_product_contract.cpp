#include "../src/PluginProcessor.h"
#include <rack_product_contract.h>

int main()
{
    return sattari::test::runRackProductContract<SattariCreateAudioProcessor> (
        "Sattari Create", 10, 9);
}
