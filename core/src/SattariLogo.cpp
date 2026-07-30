#include <sattari_audio_core/SattariLogo.h>
#include <SattariBrandAssets.h>

namespace sattari::branding
{

const juce::Image& logo()
{
    static const auto image = juce::ImageFileFormat::loadFrom (
        SattariBrandAssetsData::sattarilogowhite_png,
        SattariBrandAssetsData::sattarilogowhite_pngSize);
    return image;
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto& image = logo();
    if (! image.isValid() || bounds.isEmpty())
        return;

    g.drawImageWithin (image,
                       juce::roundToInt (bounds.getX()),
                       juce::roundToInt (bounds.getY()),
                       juce::roundToInt (bounds.getWidth()),
                       juce::roundToInt (bounds.getHeight()),
                       juce::RectanglePlacement::centred,
                       false);
}

} // namespace sattari::branding
