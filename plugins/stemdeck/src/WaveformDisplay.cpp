#include "WaveformDisplay.h"
#include <sattari_audio_core/SattariBranding.h>

namespace
{
juce::Colour accentForTitle (const juce::String& title)
{
    // Map deck letter to the brand-registry colours so each waveform matches its
    // deck card and the per-deck colour band in the editor.
    using namespace sattari::brand;
    if (title.containsIgnoreCase ("B")) return juce::Colour (kAccent);                          // mint
    if (title.containsIgnoreCase ("C")) return juce::Colour (kAccentViolet);                    // violet
    if (title.containsIgnoreCase ("D")) return juce::Colour (productAccent ("royal-chain"));    // gold
    return juce::Colour (productAccent ("stemdeck"));                                           // cyan
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
    using namespace sattari::brand;
    const auto area = getLocalBounds().toFloat();
    const auto accent = accentForTitle (title);

    // Flat panel base — deck identity already lives on the editor's deck card.
    g.setColour (juce::Colour (kPanelDeep));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (accent.withAlpha (0.35f));
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

    // Wave area fills almost the whole component now — the editor's deck card
    // shows the deck letter / filename / key chip above us, so we don't repeat them.
    auto wave = area.reduced (4.0f, 4.0f);

    // Faint vertical/horizontal grid for visual rhythm reference.
    g.setColour (juce::Colour (kHairline).withAlpha (0.45f));
    for (int i = 1; i < 8; ++i)
    {
        const float x = wave.getX() + wave.getWidth() * (float) i / 8.0f;
        g.drawVerticalLine ((int) std::round (x), wave.getY(), wave.getBottom());
    }
    g.setColour (juce::Colour (kHairline).withAlpha (0.30f));
    g.drawHorizontalLine ((int) std::round (wave.getCentreY()), wave.getX(), wave.getRight());

    if (thumbnail.getTotalLength() > 0.0)
    {
        // Glow body + sharp top stroke for that djay-Pro stacked-stereo look.
        g.setColour (accent.withAlpha (0.30f));
        thumbnail.drawChannels (g, wave.toNearestInt(), 0.0, thumbnail.getTotalLength(), 1.0f);
        g.setColour (accent.brighter (0.25f).withAlpha (0.95f));
        thumbnail.drawChannels (g, wave.toNearestInt().reduced (0, 1), 0.0, thumbnail.getTotalLength(), 0.85f);
    }
    else
    {
        // Idle placeholder bars when the deck is empty.
        juce::Random random (title.hashCode());
        const int bars = juce::jmax (18, wave.toNearestInt().getWidth() / 8);
        for (int i = 0; i < bars; ++i)
        {
            const float x = wave.getX() + (float) i * wave.getWidth() / (float) bars;
            const float h = wave.getHeight() * (0.10f + 0.78f * random.nextFloat());
            const auto r = juce::Rectangle<float> (x + 1.0f, wave.getCentreY() - h * 0.5f,
                                                   juce::jmax (2.0f, wave.getWidth() / (float) bars - 2.0f), h);
            g.setColour (accent.withAlpha (0.22f));
            g.fillRoundedRectangle (r, 1.5f);
        }
        g.setColour (juce::Colour (kTextMuted));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("DROP AUDIO", wave.toNearestInt().reduced (4), juce::Justification::centred);
    }

    // Centre playhead line.
    g.setColour (juce::Colour (kTextPrimary).withAlpha (0.55f));
    g.drawLine (wave.getCentreX(), wave.getY() - 1.0f, wave.getCentreX(), wave.getBottom() + 1.0f, 1.2f);
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
