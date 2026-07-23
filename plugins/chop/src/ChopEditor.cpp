#include "ChopEditor.h"
#include <sattari_audio_core/SattariBranding.h>

SattariChopAudioProcessorEditor::SattariChopAudioProcessorEditor (SattariChopAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (900, 620);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Chop");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Slicing sampler");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    auto setup = [this] (juce::Slider& s, const char* name)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        s.setName (name);
        addAndMakeVisible (s);
    };
    setup (sensitivitySlider, "Sensitivity");
    setup (outputSlider, "Output");
    sliderAtts.push_back (std::make_unique<SliderAttachment> (processor.parameters(), "sensitivity", sensitivitySlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (processor.parameters(), "output",      outputSlider));
}

SattariChopAudioProcessorEditor::~SattariChopAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SattariChopAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Placeholder for the waveform + slice surface (the product, §3).
    auto r = waveformArea.toFloat();
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("waveform + slice markers  —  editing surface lands here",
                waveformArea, juce::Justification::centred);

    // Slider captions.
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (auto* s : { &sensitivitySlider, &outputSlider })
        g.drawText (s->getName(), s->getBounds().withHeight (16).translated (0, -15), juce::Justification::centred);
}

void SattariChopAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (56);
    title.setBounds (top.removeFromTop (34));
    subtitle.setBounds (top.removeFromTop (20));

    area.removeFromTop (8);
    auto controls = area.removeFromBottom (96);
    padArea = area.removeFromBottom (150);        // reserved: pad grid / keyboard strip
    area.removeFromBottom (12);
    waveformArea = area;                          // the rest is the waveform surface

    auto knobRow = controls.withSizeKeepingCentre (juce::jmin (controls.getWidth(), 260), controls.getHeight());
    sensitivitySlider.setBounds (knobRow.removeFromLeft (knobRow.getWidth() / 2).reduced (6));
    outputSlider.setBounds (knobRow.reduced (6));
}
