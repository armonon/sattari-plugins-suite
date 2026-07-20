#include "HarmonyEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

namespace
{
const std::array<juce::Colour, 8> kVoiceColours {
    juce::Colour (0xff62f5c8), juce::Colour (0xff7dd3fc),
    juce::Colour (0xff34d8b4), juce::Colour (0xff8ce0d0),
    juce::Colour (0xff9b7bff), juce::Colour (0xffb59bff),
    juce::Colour (0xff5ec6e0), juce::Colour (0xff7fe0c8) };

juce::String midiName (int m)
{
    if (m < 0) return "--";
    static const char* n[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String (n[((m % 12) + 12) % 12]) + juce::String (m / 12 - 1);
}
}

SattariHarmonyAudioProcessorEditor::SattariHarmonyAudioProcessorEditor (SattariHarmonyAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      presetManager (p.parameters(), "harmony")
{
    setLookAndFeel (&lookAndFeel);
    setSize (1400, 620);   // Pro: wider for the 8-voice band

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari Harmony");
    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Diatonic vocal harmoniser - up to four voices, locked to the song key");
    addAndMakeVisible (title);
    addAndMakeVisible (subtitle);

    setupCombo (userPresets, {});
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty()) presetManager.loadUserPreset (name);
    };
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this]
    {
        sattari::midi::showLearnPopup (processor.midiLearn, midiLearnButton, processor.parameters());
    };
    refreshUserPresets();

    // Key section.
    setupCombo (sourceBox, SattariHarmonyAudioProcessor::sourceChoices());
    sourceAtt = std::make_unique<ComboAttachment> (processor.parameters(), "source", sourceBox);
    // One tactile control for the whole key story (shared across the suite):
    // Follow Song Key from the master bus, or set root+scale by hand.
    {
        sattari::ui::KeyButton::Config cfg;
        cfg.accentId   = "harmony";
        cfg.showFollow = true;                 // Harmony follows the Sattari Key bus
        cfg.showAutoKey = false;               // no self-detect here
        cfg.rootNames  = SattariHarmonyAudioProcessor::rootChoices();
        cfg.scaleNames = SattariHarmonyAudioProcessor::scaleChoices();
        cfg.hint       = "click: follow song key / set by hand";
        keyButton.setConfig (cfg);
    }
    keyButton.onLinkFollow = [this] { setKeyParam ("followKey", 1.0f); };
    keyButton.onManualKey  = [this] (int root, int scaleIdx)
    {
        setKeyParam ("followKey", 0.0f);
        setKeyParam ("root",  (float) root);
        setKeyParam ("scale", (float) scaleIdx);
    };
    addAndMakeVisible (keyButton);

    keyStatus.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    keyStatus.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (keyStatus);

    inputMeterLabel.setJustificationType (juce::Justification::centredRight);
    outputMeterLabel.setJustificationType (juce::Justification::centredRight);
    inputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kTextMuted));
    outputMeterLabel.setColour (juce::Label::textColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (inputMeterLabel);
    addAndMakeVisible (outputMeterLabel);

    // Global knobs.
    setupSlider (inputSlider,  " dB");  inputSlider.setName ("In");
    setupSlider (outputSlider, " dB");  outputSlider.setName ("Out");
    setupSlider (mixSlider);            mixSlider.setName ("Blend");
    setupSlider (tuneSlider);           tuneSlider.setName ("Auto-Tune");
    setupSlider (choirSlider);          choirSlider.setName ("Choir");
    setupSlider (spreadSlider);         spreadSlider.setName ("Spread");
    setupSlider (humanizeSlider);       humanizeSlider.setName ("Human");
    auto& params = processor.parameters();
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "input",    inputSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "output",   outputSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "mix",      mixSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "tune",     tuneSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "choir",    choirSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "spread",   spreadSlider));
    sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "humanize", humanizeSlider));

    // Voice strips.
    for (int v = 0; v < kNumVoices; ++v)
    {
        const auto s = juce::String (v + 1);
        auto& vs = voices[(size_t) v];
        vs.on.setButtonText ({});
        addAndMakeVisible (vs.on);
        buttonAtts.push_back (std::make_unique<ButtonAttachment> (params, "v" + s + "on", vs.on));

        setupCombo (vs.interval, SattariHarmonyAudioProcessor::intervalChoices());
        comboAtts.push_back (std::make_unique<ComboAttachment> (params, "v" + s + "int", vs.interval));

        setupCombo (vs.octave, { "-2 oct", "-1 oct", "0", "+1 oct", "+2 oct" });
        comboAtts.push_back (std::make_unique<ComboAttachment> (params, "v" + s + "oct", vs.octave));

        setupSlider (vs.level, " dB"); vs.level.setName ("Level");
        setupSlider (vs.pan);          vs.pan.setName ("Pan");
        setupSlider (vs.formant," st");vs.formant.setName ("Form");
        sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "v" + s + "level",   vs.level));
        sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "v" + s + "pan",     vs.pan));
        sliderAtts.push_back (std::make_unique<SliderAttachment> (params, "v" + s + "formant", vs.formant));
    }

    startTimerHz (24);
}

SattariHarmonyAudioProcessorEditor::~SattariHarmonyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariHarmonyAudioProcessorEditor::setupSlider (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    s.setTextValueSuffix (suffix);
    addAndMakeVisible (s);
}

void SattariHarmonyAudioProcessorEditor::setupCombo (juce::ComboBox& c, const juce::StringArray& items)
{
    if (! items.isEmpty())
        c.addItemList (items, 1);          // populate BEFORE any attachment (no blank dropdowns)
    addAndMakeVisible (c);
}

// Set an APVTS parameter by its real value (handles 0..1 conversion + gesture),
// used by the KeyButton callbacks instead of a live attachment.
void SattariHarmonyAudioProcessorEditor::setKeyParam (const juce::String& id, float value)
{
    if (auto* p = processor.parameters().getParameter (id))
    {
        const float norm = p->convertTo0to1 (value);
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

void SattariHarmonyAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariHarmonyAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariHarmonyAudioProcessorEditor::timerCallback()
{
    snapshot = processor.getSnapshot();

    inputMeterLabel.setText  ("IN "  + juce::String (juce::Decibels::gainToDecibels (processor.getInputMeter()  + 1.0e-6f), 1) + " dB", juce::dontSendNotification);
    outputMeterLabel.setText ("OUT " + juce::String (juce::Decibels::gainToDecibels (processor.getOutputMeter() + 1.0e-6f), 1) + " dB", juce::dontSendNotification);

    // Drive the single KeyButton: it shows the effective key + whether it's being
    // followed from the bus or set manually (the override is never silent).
    keyButton.setState (snapshot.followingBus ? sattari::ui::KeyButton::Mode::Follow
                                              : sattari::ui::KeyButton::Mode::Manual,
                        snapshot.root, snapshot.scaleChoice);

    repaint();
}

void SattariHarmonyAudioProcessorEditor::drawVoiceFan (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto r = area.toFloat();
    // Panel.
    g.setColour (juce::Colour (sattari::brand::kPanelDeep));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colour (sattari::brand::kHairline));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (juce::Colour (sattari::brand::kTextFaint));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText ("HARMONY VOICES", r.reduced (12.0f, 8.0f).toNearestInt(), juce::Justification::topLeft);

    if (! snapshot.voiced || snapshot.leadMidi < 0)
    {
        g.setColour (juce::Colour (sattari::brand::kTextFaint));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Sing or play a vocal to build the choir", area, juce::Justification::centred);
        return;
    }

    const float pxPerSemi = juce::jmin (12.0f, r.getHeight() / 30.0f);
    const juce::Point<float> anchor (r.getX() + 80.0f, r.getCentreY());
    const float voiceX = r.getRight() - 90.0f;

    auto pitchY = [&] (int midi)
    {
        return juce::jlimit (r.getY() + 16.0f, r.getBottom() - 16.0f,
                             anchor.y - (float) (midi - snapshot.leadMidi) * pxPerSemi);
    };

    // Voices fan out from the lead.
    for (int v = 0; v < kNumVoices; ++v)
    {
        if (! snapshot.voiceOn[(size_t) v] || snapshot.voiceMidi[(size_t) v] < 0) continue;
        const auto col = kVoiceColours[(size_t) v];
        const float y = pitchY (snapshot.voiceMidi[(size_t) v]);

        juce::Path fan;
        fan.startNewSubPath (anchor);
        fan.cubicTo (anchor.x + 60.0f, anchor.y, voiceX - 60.0f, y, voiceX, y);
        g.setColour (col.withAlpha (0.5f));
        g.strokePath (fan, juce::PathStrokeType (1.6f));

        g.setColour (col);
        g.fillEllipse (voiceX - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (midiName (snapshot.voiceMidi[(size_t) v]),
                    (int) voiceX + 10, (int) y - 8, 50, 16, juce::Justification::left);
    }

    // Lead marker on top.
    g.setColour (juce::Colour (sattari::brand::kAccent));
    g.fillEllipse (anchor.x - 6.0f, anchor.y - 6.0f, 12.0f, 12.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("LEAD " + midiName (snapshot.leadMidi),
                (int) anchor.x - 92, (int) anchor.y - 8, 84, 16, juce::Justification::right);
}

void SattariHarmonyAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());

    // Knob captions (drawn above each rotary, like the rest of the suite).
    auto caption = [&] (juce::Slider& s)
    {
        if (s.getName().isEmpty()) return;
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s.getName(), s.getBounds().withHeight (16).translated (0, -15), juce::Justification::centred);
    };
    for (auto* s : { &inputSlider, &outputSlider, &mixSlider, &tuneSlider, &choirSlider, &spreadSlider, &humanizeSlider })
        caption (*s);
    for (auto& vs : voices)
        for (auto* s : { &vs.level, &vs.pan, &vs.formant })
            caption (*s);

    // Voice-strip headers.
    g.setColour (juce::Colour (sattari::brand::kTextMuted));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto b = voices[(size_t) v].interval.getBounds();
        g.setColour (kVoiceColours[(size_t) v].withAlpha (0.9f));
        g.drawText ("VOICE " + juce::String (v + 1),
                    b.getX(), b.getY() - 40, b.getWidth(), 16, juce::Justification::centredLeft);
    }

    drawVoiceFan (g, fanArea);
}

void SattariHarmonyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    auto top = area.removeFromTop (70);
    auto titleCol = top.removeFromLeft (420);
    title.setBounds (titleCol.removeFromTop (40));
    subtitle.setBounds (titleCol.removeFromTop (24));
    auto meters = top.removeFromRight (150);
    inputMeterLabel.setBounds (meters.removeFromTop (24));
    outputMeterLabel.setBounds (meters.removeFromTop (24));

    auto presetRow = area.removeFromTop (32);
    userPresets.setBounds (presetRow.removeFromLeft (190));
    presetRow.removeFromLeft (10);
    savePresetButton.setBounds (presetRow.removeFromLeft (110));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (120));
    area.removeFromTop (10);

    auto keyRow = area.removeFromTop (56);
    sourceBox.setBounds (keyRow.removeFromLeft (130).withSizeKeepingCentre (130, 28));
    keyRow.removeFromLeft (12);
    keyButton.setBounds (keyRow.removeFromLeft (juce::jmin (320, keyRow.getWidth())));
    area.removeFromTop (12);

    fanArea = area.removeFromTop (130);
    area.removeFromTop (16);

    // Four voice columns.
    auto voicesBand = area.removeFromTop (200);
    const int colW = voicesBand.getWidth() / kNumVoices;
    for (int v = 0; v < kNumVoices; ++v)
    {
        auto col = voicesBand.removeFromLeft (colW).reduced (8, 0);
        col.removeFromTop (20);                      // header caption (painted)
        auto onRow = col.removeFromTop (24);
        voices[(size_t) v].on.setBounds (onRow.removeFromLeft (40));
        voices[(size_t) v].octave.setBounds (onRow);   // octave selector beside the On toggle
        voices[(size_t) v].interval.setBounds (col.removeFromTop (26));
        col.removeFromTop (24);                       // knob captions
        auto knobs = col.removeFromTop (96);
        const int kw = knobs.getWidth() / 3;
        voices[(size_t) v].level.setBounds   (knobs.removeFromLeft (kw).reduced (2));
        voices[(size_t) v].pan.setBounds     (knobs.removeFromLeft (kw).reduced (2));
        voices[(size_t) v].formant.setBounds (knobs.removeFromLeft (kw).reduced (2));
    }

    area.removeFromTop (10);
    auto globals = area.removeFromTop (96);
    auto globalRow = globals.withSizeKeepingCentre (juce::jmin (globals.getWidth(), 770), globals.getHeight());
    const int gw = globalRow.getWidth() / 7;
    inputSlider.setBounds    (globalRow.removeFromLeft (gw).reduced (4));
    mixSlider.setBounds      (globalRow.removeFromLeft (gw).reduced (4));
    tuneSlider.setBounds     (globalRow.removeFromLeft (gw).reduced (4));
    choirSlider.setBounds    (globalRow.removeFromLeft (gw).reduced (4));
    spreadSlider.setBounds   (globalRow.removeFromLeft (gw).reduced (4));
    humanizeSlider.setBounds (globalRow.removeFromLeft (gw).reduced (4));
    outputSlider.setBounds   (globalRow.removeFromLeft (gw).reduced (4));
}
