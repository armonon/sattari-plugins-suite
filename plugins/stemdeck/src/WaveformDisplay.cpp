#include "WaveformDisplay.h"

namespace
{
juce::Colour accentForTitle (const juce::String& title)
{
    if (title.containsIgnoreCase ("B")) return juce::Colour (0xffff4c5f);
    if (title.containsIgnoreCase ("C")) return juce::Colour (0xff50f06b);
    if (title.containsIgnoreCase ("D")) return juce::Colour (0xffffcf4a);
    return juce::Colour (0xff26d9ff);
}
}

WaveformDisplay::WaveformDisplay (juce::AudioFormatManager& manager, juce::AudioThumbnailCache& cache, juce::String deckName)
    : thumbnail (512, manager, cache), title (std::move (deckName))
{
    thumbnail.addChangeListener (this);
}

WaveformDisplay::~WaveformDisplay()
{
    thumbnail.removeChangeListener (this);
}

void WaveformDisplay::setOnFileDropped (std::function<void(const juce::File&)> callback)
{
    onFileDropped = std::move (callback);
}

void WaveformDisplay::setFile (const juce::File& file)
{
    if (file == currentFile)
        return;

    currentFile = file;
    thumbnail.clear();
    if (file.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (file));
    repaint();
}

void WaveformDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (2.0f);
    const auto accent = accentForTitle (title);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff06070c), area.getTopLeft(),
                                             juce::Colour (0xff141725), area.getBottomRight(), false));
    g.fillRoundedRectangle (area, 10.0f);
    g.setColour (accent.withAlpha (0.48f));
    g.drawRoundedRectangle (area, 10.0f, 1.2f);

    g.setColour (accent.withAlpha (0.12f));
    g.fillRoundedRectangle (area.reduced (4.0f).removeFromBottom (area.getHeight() * 0.68f), 8.0f);

    auto textArea = getLocalBounds().reduced (12, 8);
    g.setColour (juce::Colours::white.withAlpha (0.88f));
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    auto titleRow = textArea.removeFromTop (22);
    g.drawText (title, titleRow.removeFromLeft (78), juce::Justification::centredLeft);
    g.setColour (accent.withAlpha (0.94f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("STEMS  WAVE  GRID", titleRow, juce::Justification::centredRight);

    auto wave = getLocalBounds().reduced (12, 34).toFloat();
    g.setColour (juce::Colour (0xff090b13));
    g.fillRoundedRectangle (wave, 8.0f);

    g.setColour (juce::Colours::white.withAlpha (0.055f));
    for (int i = 1; i < 8; ++i)
    {
        const float x = wave.getX() + wave.getWidth() * (float) i / 8.0f;
        g.drawVerticalLine ((int) std::round (x), wave.getY(), wave.getBottom());
    }
    for (int i = 1; i < 4; ++i)
    {
        const float y = wave.getY() + wave.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine ((int) std::round (y), wave.getX(), wave.getRight());
    }

    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour (accent.withAlpha (0.28f));
        thumbnail.drawChannels (g, wave.toNearestInt(), 0.0, thumbnail.getTotalLength(), 1.0f);
        g.setColour (accent.brighter (0.22f));
        thumbnail.drawChannels (g, wave.toNearestInt(), 0.0, thumbnail.getTotalLength(), 0.85f);
    }
    else
    {
        juce::Random random (title.hashCode());
        const int bars = juce::jmax (18, wave.toNearestInt().getWidth() / 9);
        for (int i = 0; i < bars; ++i)
        {
            const float x = wave.getX() + (float) i * wave.getWidth() / (float) bars;
            const float h = wave.getHeight() * (0.14f + 0.76f * random.nextFloat());
            const auto r = juce::Rectangle<float> (x + 1.0f, wave.getCentreY() - h * 0.5f, juce::jmax (2.0f, wave.getWidth() / (float) bars - 2.0f), h);
            g.setColour (accent.interpolatedWith (juce::Colours::white, random.nextFloat() * 0.35f).withAlpha (0.35f));
            g.fillRoundedRectangle (r, 2.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.62f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Drop audio here", wave.toNearestInt().reduced (4), juce::Justification::centred);
    }

    g.setColour (juce::Colours::white.withAlpha (0.82f));
    g.drawLine (wave.getCentreX(), wave.getY() - 2.0f, wave.getCentreX(), wave.getBottom() + 2.0f, 1.4f);
    g.setColour (accent.withAlpha (0.5f));
    g.drawRoundedRectangle (wave, 8.0f, 1.0f);
}

bool WaveformDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3")
            return true;
    }
    return false;
}

void WaveformDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;

    const juce::File file (files[0]);
    if (onFileDropped)
        onFileDropped (file);
    setFile (file);
}

void WaveformDisplay::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}
