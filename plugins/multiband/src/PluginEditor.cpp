#include "PluginEditor.h"

namespace
{
const char* const bandNames[3]   = { "LOW", "MID", "HIGH" };
const char* const bandPrefix[3]  = { "0", "1", "2" };   // params use thr1/thr2/thr3 (band+1)

struct KnobDef { const char* base; const char* cap; const char* suffix; };
const KnobDef perBandKnobs[5] = {
    { "thr",   "THRESH",  " dB" },
    { "ratio", "RATIO",   ":1"  },
    { "atk",   "ATTACK",  " ms" },
    { "rel",   "RELEASE", " ms" },
    { "mk",    "MAKEUP",  " dB" },
};

juce::String bandParamId (const char* base, int band) { return juce::String (base) + juce::String (band + 1); }
} // namespace

SattariMultibandAudioProcessorEditor::SattariMultibandAudioProcessorEditor (SattariMultibandAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    juce::ignoreUnused (bandPrefix);
    setLookAndFeel (&lookAndFeel);
    setSize (760, 470);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Multiband");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "3-band multiband compressor");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    for (int b = 0; b < kBands; ++b)
    {
        auto& ui = bands[(size_t) b];

        ui.header.setText (bandNames[b], juce::dontSendNotification);
        ui.header.setFont (sattari::brand::fontOptions (13.0f, juce::Font::bold));
        ui.header.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
        ui.header.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (ui.header);

        for (int k = 0; k < kPerBand; ++k)
        {
            styleKnob (ui.knobs[(size_t) k], perBandKnobs[k].suffix);
            styleCaption (ui.caps[(size_t) k], perBandKnobs[k].cap);
            addAndMakeVisible (ui.knobs[(size_t) k]);
            addAndMakeVisible (ui.caps[(size_t) k]);
            ui.atts[(size_t) k] = std::make_unique<SliderAttachment> (
                processor.parameters(), bandParamId (perBandKnobs[k].base, b), ui.knobs[(size_t) k]);
        }

        ui.solo.setButtonText ("SOLO");
        ui.solo.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (ui.solo);
        ui.soloAtt = std::make_unique<ButtonAttachment> (
            processor.parameters(), bandParamId ("solo", b), ui.solo);
    }

    struct G { juce::Slider* s; juce::Label* c; const char* id; const char* cap; const char* suffix; };
    const G gs[] = {
        { &xLow,   &xLowCap,   "xLow",   "LOW/MID X",  " Hz" },
        { &xHigh,  &xHighCap,  "xHigh",  "MID/HIGH X", " Hz" },
        { &mix,    &mixCap,    "mix",    "MIX",        " %"  },
        { &output, &outputCap, "output", "OUTPUT",     " dB" },
    };
    int gi = 0;
    for (auto& g : gs)
    {
        styleKnob (*g.s, g.suffix);
        styleCaption (*g.c, g.cap);
        addAndMakeVisible (*g.s);
        addAndMakeVisible (*g.c);
        globalAtts[(size_t) gi++] = std::make_unique<SliderAttachment> (processor.parameters(), g.id, *g.s);
    }

    startTimerHz (24);
}

SattariMultibandAudioProcessorEditor::~SattariMultibandAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariMultibandAudioProcessorEditor::styleKnob (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (sattari::brand::kAccent));
    s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (sattari::brand::kHairline));
    s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (sattari::brand::kPanelDeep));
    if (suffix.isNotEmpty()) s.setTextValueSuffix (suffix);
}

void SattariMultibandAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& t)
{
    l.setText (t, juce::dontSendNotification);
    l.setFont (sattari::brand::fontOptions (9.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariMultibandAudioProcessorEditor::timerCallback()
{
    for (int b = 0; b < kBands; ++b)
    {
        auto& ui = bands[(size_t) b];
        const float gr = processor.getBandGainReductionDb (b);
        // Fast attack on the meter, slower fall — reads like a real GR meter.
        ui.grSmoothed += (gr - ui.grSmoothed) * (gr > ui.grSmoothed ? 0.6f : 0.15f);
    }
    repaint();
}

void SattariMultibandAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    for (int b = 0; b < kBands; ++b)
    {
        const auto& ui = bands[(size_t) b];
        if (ui.meterBounds.isEmpty()) continue;
        const auto r = ui.meterBounds.toFloat();
        g.setColour (juce::Colour (sattari::brand::kPanelRaised));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (sattari::brand::kHairline));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        constexpr float maxGr = 24.0f;                               // meter spans 0..24 dB
        const float frac = juce::jlimit (0.0f, 1.0f, ui.grSmoothed / maxGr);
        auto fill = r.reduced (2.0f);
        fill = fill.withY (fill.getBottom() - fill.getHeight() * frac)
                   .withHeight (fill.getHeight() * frac);            // grows upward from the bottom
        g.setColour (juce::Colour (sattari::brand::productAccent ("multiband")));
        g.fillRoundedRectangle (fill, 3.0f);
    }
}

void SattariMultibandAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);

    title.setBounds (b.removeFromTop (32));
    subtitle.setBounds (b.removeFromTop (20));
    b.removeFromTop (10);

    // Bottom row: crossovers + global mix/output.
    auto globals = b.removeFromBottom (110);
    {
        juce::Slider* sl[] = { &xLow, &xHigh, &mix, &output };
        juce::Label*  ca[] = { &xLowCap, &xHighCap, &mixCap, &outputCap };
        const int w = globals.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = globals.removeFromLeft (w);
            ca[i]->setBounds (cell.removeFromBottom (14));
            sl[i]->setBounds (cell.reduced (10, 4));
        }
    }
    b.removeFromBottom (8);

    // Three band columns fill the remaining space.
    const int colW = b.getWidth() / kBands;
    for (int bi = 0; bi < kBands; ++bi)
    {
        auto& ui = bands[(size_t) bi];
        auto col = b.removeFromLeft (bi == kBands - 1 ? b.getWidth() : colW).reduced (4, 0);

        ui.header.setBounds (col.removeFromTop (20));

        // Solo toggle under the header.
        auto soloRow = col.removeFromTop (24);
        ui.solo.setBounds (soloRow.removeFromLeft (70));
        // GR meter runs down the right edge of the column.
        ui.meterBounds = col.removeFromRight (16).reduced (0, 2);
        col.removeFromRight (6);

        // 5 knobs in a compact grid (3 on top, 2 on bottom).
        auto placeKnob = [&] (int k, juce::Rectangle<int> cell)
        {
            ui.caps[(size_t) k].setBounds (cell.removeFromBottom (12));
            ui.knobs[(size_t) k].setBounds (cell.reduced (4, 0));
        };
        const int rowH = col.getHeight() / 2;
        auto top = col.removeFromTop (rowH);
        auto bot = col;
        const int topW = top.getWidth() / 3;
        placeKnob (0, top.removeFromLeft (topW));
        placeKnob (1, top.removeFromLeft (topW));
        placeKnob (2, top);
        const int botW = bot.getWidth() / 2;
        placeKnob (3, bot.removeFromLeft (botW));
        placeKnob (4, bot);
    }
}
