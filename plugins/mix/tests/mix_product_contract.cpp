#include "../src/PluginProcessor.h"
#include <rack_product_contract.h>

int main()
{
    return sattari::test::runRackProductContract<SattariMixAudioProcessor> (
        "Sattari Mix", 14, 8);
}
