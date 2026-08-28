#include "PluginEditor.h"

SattariWarpAudioProcessorEditor::SattariWarpAudioProcessorEditor (SattariWarpAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (760, 520);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Warp");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Creative vocal transformer — one Dose takes you deeper");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    auto& params = processor.parameters();

    styleKnob (dose, true);
    styleCaption (doseCaption, "DOSE");
    addAndMakeVisible (dose);
    addAndMakeVisible (doseCaption);
    doseAtt = std::make_unique<SliderAttachment> (params, "dose", dose);

    struct K { juce::Slider* s; juce::Label* c; const char* id; const char* cap; };
    const K ks[] = {
        { &vowel,     &vowelCap,     "vowel",     "VOWEL" },
        { &vowelAmt,  &vowelAmtCap,  "vowelAmt",  "VOWEL AMT" },
        { &robotFreq, &robotFreqCap, "robotFreq", "ROBOT FREQ" },
        { &robotAmt,  &robotAmtCap,  "robotAmt",  "ROBOT" },
        { &drive,     &driveCap,     "drive",     "DRIVE" },
        { &crush,     &crushCap,     "crush",     "CRUSH" },
        { &stutter,   &stutterCap,   "stutter",   "STUTTER" },
        { &input,     &inputCap,     "input",     "INPUT" },
        { &output,    &outputCap,    "output",    "OUTPUT" },
        { &mix,       &mixCap,       "mix",       "MIX" },
    };
    for (auto& k : ks)
    {
        styleKnob (*k.s);
        styleCaption (*k.c, k.cap);
        addAndMakeVisible (*k.s);
        addAndMakeVisible (*k.c);
    }
    vowelAtt     = std::make_unique<SliderAttachment> (params, "vowel", vowel);
    vowelAmtAtt  = std::make_unique<SliderAttachment> (params, "vowelAmt", vowelAmt);
    robotFreqAtt = std::make_unique<SliderAttachment> (params, "robotFreq", robotFreq);
    robotAmtAtt  = std::make_unique<SliderAttachment> (params, "robotAmt", robotAmt);
    driveAtt     = std::make_unique<SliderAttachment> (params, "drive", drive);
    crushAtt     = std::make_unique<SliderAttachment> (params, "crush", crush);
    stutterAtt   = std::make_unique<SliderAttachment> (params, "stutter", stutter);
    inputAtt     = std::make_unique<SliderAttachment> (params, "input", input);
    outputAtt    = std::make_unique<SliderAttachment> (params, "output", output);
    mixAtt       = std::make_unique<SliderAttachment> (params, "mix", mix);

    stutterRate.addItemList ({ "1/4", "1/8", "1/8T", "1/16", "1/32" }, 1);
    addAndMakeVisible (stutterRate);
    styleCaption (stutterRateCap, "RATE");
    addAndMakeVisible (stutterRateCap);
    stutterRateAtt = std::make_unique<ComboAttachment> (params, "stutterRate", stutterRate);

    midiLearnButton.onClick = [this] {
        sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
    };
    addAndMakeVisible (midiLearnButton);

    startTimerHz (30);
}

SattariWarpAudioProcessorEditor::~SattariWarpAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariWarpAudioProcessorEditor::styleKnob (juce::Slider& s, bool big)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 100 : 64, big ? 22 : 16);
}

void SattariWarpAudioProcessorEditor::styleCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (sattari::brand::fontOptions (10.0f, juce::Font::bold));
    l.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    l.setJustificationType (juce::Justification::centred);
}

void SattariWarpAudioProcessorEditor::timerCallback()
{
    pulse += 0.06f + 0.20f * processor.getOutputLevel();   // spin faster when louder
    repaint();
}

void SattariWarpAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Hypnotic spiral behind the DOSE knob — reacts to output level and dose.
    const auto accent = juce::Colour (sattari::brand::productAccent ("warp"));
    const float lvl = juce::jlimit (0.0f, 1.0f, processor.getOutputLevel());
    const float cx = getWidth() * 0.5f, cy = 232.0f;
    juce::Path spiral;
    const int turns = 5;
    const float maxR = 150.0f;
    for (float a = 0.0f; a < turns * juce::MathConstants<float>::twoPi; a += 0.12f)
    {
        const float r = (a / (turns * juce::MathConstants<float>::twoPi)) * maxR;
        const float x = cx + std::cos (a + pulse) * r;
        const float y = cy + std::sin (a + pulse) * r;
        if (a == 0.0f) spiral.startNewSubPath (x, y);
        else           spiral.lineTo (x, y);
    }
    g.setColour (accent.withAlpha (0.10f + 0.22f * lvl));
    g.strokePath (spiral, juce::PathStrokeType (1.2f));
}

void SattariWarpAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (18);

    auto header = area.removeFromTop (54);
    title.setBounds (header.removeFromTop (30));
    subtitle.setBounds (header);
    midiLearnButton.setBounds (getLocalBounds().removeFromTop (40).removeFromRight (130).reduced (6, 6));

    // Big DOSE in the centre.
    auto centre = area.removeFromTop (250);
    const int dw = 190;
    dose.setBounds (centre.withSizeKeepingCentre (dw, dw - 16));
    doseCaption.setBounds ((int) (getWidth() * 0.5f - 60), centre.getBottom() - 4, 120, 14);

    // Two knob rows below.
    auto lay = [] (juce::Rectangle<int> row, std::initializer_list<juce::Component*> knobs,
                   std::initializer_list<juce::Label*> caps)
    {
        const int n = (int) knobs.size();
        const int w = row.getWidth() / n;
        auto k = knobs.begin(); auto c = caps.begin();
        for (int i = 0; i < n; ++i, ++k, ++c)
        {
            auto cell = row.removeFromLeft (w);
            (*c)->setBounds (cell.removeFromTop (14));
            (*k)->setBounds (cell);
        }
    };

    area.removeFromTop (6);
    auto row1 = area.removeFromTop (100);
    lay (row1, { &vowel, &vowelAmt, &robotFreq, &robotAmt, &drive }, { &vowelCap, &vowelAmtCap, &robotFreqCap, &robotAmtCap, &driveCap });

    area.removeFromTop (6);
    auto row2 = area.removeFromTop (100);
    // crush, stutter, rate combo, mix, input+output share the last row
    const int w = row2.getWidth() / 6;
    auto crushCell = row2.removeFromLeft (w);   crushCap.setBounds (crushCell.removeFromTop (14));   crush.setBounds (crushCell);
    auto stCell    = row2.removeFromLeft (w);   stutterCap.setBounds (stCell.removeFromTop (14));    stutter.setBounds (stCell);
    auto rateCell  = row2.removeFromLeft (w);   stutterRateCap.setBounds (rateCell.removeFromTop (14)); stutterRate.setBounds (rateCell.removeFromTop (28).reduced (4, 0));
    auto mixCell   = row2.removeFromLeft (w);   mixCap.setBounds (mixCell.removeFromTop (14));       mix.setBounds (mixCell);
    auto inCell    = row2.removeFromLeft (w);   inputCap.setBounds (inCell.removeFromTop (14));      input.setBounds (inCell);
    auto outCell   = row2;                      outputCap.setBounds (outCell.removeFromTop (14));    output.setBounds (outCell);
}
