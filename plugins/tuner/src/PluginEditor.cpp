#include "PluginEditor.h"
#include <cmath>

SattariTunerAudioProcessorEditor::SattariTunerAudioProcessorEditor (SattariTunerAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (620, 460);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Tuner");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Precision instrument & vocal tuner");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);
    addAndMakeVisible (sessionHud);

    auto& params = processor.parameters();

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; };
    const K ks[] = {
        { &referenceA, &referenceACap, "referenceA", "REF A" },
        { &smoothing,  &smoothingCap,  "smoothing",  "SMOOTHING" },
        { &transpose,  &transposeCap,  "transpose",  "TRANSPOSE" },
    };
    for (auto& k : ks)
    {
        styleKnob (*k.s);
        styleCaption (*k.c, k.cap);
        addAndMakeVisible (*k.s);
        addAndMakeVisible (*k.c);
    }
    referenceA.setTextValueSuffix (" Hz");
    referenceAAtt = std::make_unique<SliderAttachment> (params, "referenceA", referenceA);
    smoothingAtt  = std::make_unique<SliderAttachment> (params, "smoothing", smoothing);
    transposeAtt  = std::make_unique<SliderAttachment> (params, "transpose", transpose);

    startTimerHz (30);
}

SattariTunerAudioProcessorEditor::~SattariTunerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariTunerAudioProcessorEditor::styleKnob (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
}

void SattariTunerAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariTunerAudioProcessorEditor::timerCallback()
{
    const auto r = processor.getReading();
    const float target = r.voiced ? juce::jlimit (-50.0f, 50.0f, r.cents) : 0.0f;
    // Ease the needle toward the reading; relax to centre when unvoiced.
    needleCents += (target - needleCents) * (r.voiced ? 0.35f : 0.12f);

    const bool inTune = r.voiced && std::abs (r.cents) < 5.0f;
    tuneGlow += ((inTune ? 1.0f : 0.0f) - tuneGlow) * 0.2f;
    repaint();
}

void SattariTunerAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
    paintTunerFace (g, getLocalBounds().toFloat().withTrimmedTop (86.0f).withTrimmedBottom (128.0f)
                                                  .reduced (28.0f, 8.0f));
}

void SattariTunerAudioProcessorEditor::paintTunerFace (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto accent  = juce::Colour (sattari::brand::productAccent ("tuner"));
    const auto green    = juce::Colour (sattari::brand::kSuccess);
    const auto muted    = juce::Colour (sattari::brand::kTextMuted);
    const auto faint    = juce::Colour (sattari::brand::kHairline);

    const auto r = processor.getReading();

    // --- Big note name -------------------------------------------------------
    const auto noteStr = r.voiced ? SattariTunerAudioProcessor::noteName (r.midiNote)
                                  : juce::String ("--");
    auto glowCol = green.withMultipliedAlpha (tuneGlow).interpolatedWith (accent, 1.0f - tuneGlow);
    const auto noteCol = r.voiced ? glowCol : muted.withAlpha (0.5f);

    auto noteArea = area.removeFromTop (area.getHeight() * 0.52f);
    g.setColour (noteCol);
    g.setFont (juce::FontOptions (96.0f, juce::Font::bold));
    g.drawText (noteStr, noteArea, juce::Justification::centred);

    // --- Cents meter (needle over a -50..+50 scale) --------------------------
    area.removeFromTop (6.0f);
    auto meter = area.removeFromTop (56.0f);
    const float cx = meter.getCentreX();
    const float top = meter.getY(), bot = meter.getBottom();
    const float halfW = meter.getWidth() * 0.5f - 8.0f;

    // Ticks every 10 cents; centre tick emphasised.
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = cx + (float) c / 50.0f * halfW;
        const bool centre = (c == 0);
        g.setColour (centre ? green.withAlpha (0.9f) : faint);
        const float h = centre ? 22.0f : 12.0f;
        g.fillRect (x - (centre ? 1.5f : 0.75f), top, centre ? 3.0f : 1.5f, h);
    }
    // In-tune zone shading (+/-5 cents).
    g.setColour (green.withAlpha (0.10f + 0.15f * tuneGlow));
    g.fillRect (cx - 5.0f / 50.0f * halfW, top, 2.0f * 5.0f / 50.0f * halfW, 26.0f);

    // Needle.
    const float nx = cx + juce::jlimit (-1.0f, 1.0f, needleCents / 50.0f) * halfW;
    const bool inTune = r.voiced && std::abs (r.cents) < 5.0f;
    g.setColour (r.voiced ? (inTune ? green : accent) : muted.withAlpha (0.4f));
    g.fillRoundedRectangle (nx - 2.0f, top - 4.0f, 4.0f, bot - top + 8.0f, 2.0f);

    // --- Readout line: cents + frequency + arrows ----------------------------
    area.removeFromTop (10.0f);
    auto readout = area.removeFromTop (26.0f);
    g.setFont (juce::FontOptions (14.0f));
    if (r.voiced)
    {
        const int cents = (int) std::lround (r.cents);
        const juce::String arrow = cents <= -3 ? juce::String::fromUTF8 ("◀ flat")
                                 : cents >=  3 ? juce::String::fromUTF8 ("sharp ▶")
                                               : juce::String::fromUTF8 ("✓ in tune");
        g.setColour (inTune ? green : muted);
        g.drawText (juce::String (cents > 0 ? "+" : "") + juce::String (cents) + " cents   "
                    + juce::String (r.frequencyHz, 1) + " Hz   " + arrow,
                    readout, juce::Justification::centred);
    }
    else
    {
        g.setColour (muted.withAlpha (0.6f));
        g.drawText ("play a note...", readout, juce::Justification::centred);
    }
}

void SattariTunerAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (18);
    sessionHud.setBounds (b.removeFromRight (156).removeFromTop (24).reduced (0, 1));
    title.setBounds (b.removeFromTop (34));
    subtitle.setBounds (b.removeFromTop (20));

    // Three knobs along the bottom.
    auto row = getLocalBounds().removeFromBottom (110).reduced (40, 8);
    const int w = row.getWidth() / 3;
    auto place = [&] (juce::Slider& s, juce::Label& c, juce::Rectangle<int> cell)
    {
        c.setBounds (cell.removeFromBottom (16));
        s.setBounds (cell.reduced (6, 0));
    };
    place (referenceA, referenceACap, row.removeFromLeft (w));
    place (smoothing,  smoothingCap,  row.removeFromLeft (w));
    place (transpose,  transposeCap,  row);
}
