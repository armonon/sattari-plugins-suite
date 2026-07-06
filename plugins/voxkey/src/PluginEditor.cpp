#include "PluginEditor.h"
#include <sattari_audio_core/SattariMidiLearn.h>

void VoxKeyOrb::setState (const VoxKeyVoiceMidiMapper::State& newState)
{
    state = newState;
    repaint();
}

void VoxKeyOrb::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (8.0f);
    g.setColour (juce::Colour (0xff080913));
    g.fillRoundedRectangle (area, 22.0f);

    const auto glow = state.midiActive ? juce::Colour (sattari::brand::kAccent) : (state.voiced ? juce::Colour (0xffffb45e) : juce::Colour (0xff4b4f66));
    g.setColour (glow.withAlpha (0.32f));
    g.fillEllipse (area.reduced (36.0f));
    g.setColour (glow);
    g.drawEllipse (area.reduced (36.0f), 3.0f);

    g.setFont (juce::FontOptions (48.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawFittedText (state.outputName, area.toNearestInt().withTrimmedBottom (60), juce::Justification::centred, 1);

    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colours::lightgrey);
    g.drawFittedText ("detected " + state.detectedName + "  •  confidence " + juce::String ((int) std::round (state.confidence * 100.0f)) + "%",
                      area.toNearestInt().removeFromBottom (42), juce::Justification::centred, 2);

    const float level = juce::jlimit (0.0f, 1.0f, state.inputRms * 18.0f);
    auto meter = area.reduced (18.0f).removeFromBottom (8.0f);
    g.setColour (juce::Colour (0xff202439));
    g.fillRoundedRectangle (meter, 4.0f);
    g.setColour (glow);
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * level), 4.0f);
}

SattariVoxKeyAudioProcessorEditor::SattariVoxKeyAudioProcessorEditor (SattariVoxKeyAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "voxkey")
{
    setLookAndFeel (&lookAndFeel);
    // Tall enough that the full left column fits: header + two knob rows with
    // captions + the toggle row (Pitch Bend / Glide / Bypass / Panic / Follow
    // Song Key) + the routing help label. (Previously 520, which clipped the
    // toggle row and routing label off the bottom.)
    setSize (860, 630);

    sattari::ui::SattariLookAndFeel::styleTitle (title, "Sattari VoxKey");
    addAndMakeVisible (title);

    sattari::ui::SattariLookAndFeel::styleSubtitle (subtitle, "Voice-to-MIDI instrument with dry vocal pass-through");
    addAndMakeVisible (subtitle);

    setupCombo (userPresets);
    userPresets.setTextWhenNothingSelected ("User presets...");
    userPresets.onChange = [this]
    {
        const auto name = userPresets.getText();
        if (name.isNotEmpty())
            presetManager.loadUserPreset (name);
    };
    savePresetButton.onClick = [this] { saveCurrentAsUserPreset(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (abRow);
    addAndMakeVisible (midiLearnButton);
    midiLearnButton.onClick = [this] {
        sattari::midi::showLearnPopup (audioProcessor.midiLearn, midiLearnButton, audioProcessor.parameters());
    };
    refreshUserPresets();

    setupCombo (mode);
    mode.addItemList ({ "Melody", "Bass Follow", "Harmony", "Drum Mouth", "Pad/Soul Layer" }, 1);
    setupCombo (key);
    key.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    setupCombo (scale);
    scale.addItemList ({ "Chromatic", "Major", "Minor", "Pentatonic" }, 1);

    setupSlider (sensitivity);
    setupSlider (smoothness);
    setupSlider (confidence);
    setupSlider (octave);
    setupSlider (hold, " ms");
    setupSlider (release, " ms");
    setupSlider (velocity);
    setupSlider (pitchBendRange, " st");

    sensitivity.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    smoothness.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    confidence.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };
    velocity.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; };

    setupToggle (pitchBend);
    pitchBend.setButtonText ("Pitch Bend");
    setupToggle (glide);
    glide.setButtonText ("Glide / Legato");
    setupToggle (bypass);
    bypass.setButtonText ("Bypass");
    setupToggle (followKey);
    followKey.setButtonText ("Follow Song Key");
    setupToggle (neuralPitch);
    neuralPitch.setButtonText ("Neural Pitch");
    // Experimental "alpha" features are hidden from the shipping UI so users never
    // see a half-validated toggle. Build with -DSATTARI_ENABLE_EXPERIMENTAL=1 to
    // expose them for development. The param itself stays registered (default off)
    // so saved sessions remain compatible either way.
   #if SATTARI_ENABLE_EXPERIMENTAL
    setupToggle (paraphonic);
    paraphonic.setButtonText ("Paraphonic (alpha)");
   #endif

    panicButton.onClick = [this] { audioProcessor.panic(); };
    panicButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a1320));
    panicButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible (panicButton);

    setupLabel (inputLabel, "Performance");
    setupLabel (routingLabel, "Ableton routing: Audio track has VoxKey; MIDI track receives MIDI From VoxKey, Monitor In/Auto.");
    setupLabel (detectedLabel, "Detected: --");
    setupLabel (outputLabel, "MIDI Out: --");
    setupLabel (confidenceLabel, "MIDI activity: idle");

    // Per-knob + combo name captions (centred above each control).
    using LabelCaption = std::pair<juce::Label*, const char*>;
    for (const LabelCaption lt : { LabelCaption { &modeLabel, "MODE" },
                                   LabelCaption { &keyLabel, "KEY" },
                                   LabelCaption { &scaleLabel, "SCALE" },
                                   LabelCaption { &sensitivityLabel, "SENSITIVITY" },
                                   LabelCaption { &smoothnessLabel, "SMOOTHNESS" },
                                   LabelCaption { &confidenceKnobLabel, "CONFIDENCE" },
                                   LabelCaption { &octaveLabel, "OCTAVE" },
                                   LabelCaption { &holdLabel, "HOLD" },
                                   LabelCaption { &releaseLabel, "RELEASE" },
                                   LabelCaption { &velocityLabel, "VELOCITY" },
                                   LabelCaption { &bendRangeLabel, "BEND RANGE" } })
    {
        setupLabel (*lt.first, lt.second, 10.0f);
        lt.first->setJustificationType (juce::Justification::centred);
    }
    addAndMakeVisible (orb);

    auto& params = audioProcessor.parameters();
    modeAttach = std::make_unique<ComboAttachment> (params, "mode", mode);
    keyAttach = std::make_unique<ComboAttachment> (params, "key", key);
    scaleAttach = std::make_unique<ComboAttachment> (params, "scale", scale);
    sensitivityAttach = std::make_unique<SliderAttachment> (params, "sensitivity", sensitivity);
    smoothnessAttach = std::make_unique<SliderAttachment> (params, "smoothness", smoothness);
    confidenceAttach = std::make_unique<SliderAttachment> (params, "confidence", confidence);
    octaveAttach = std::make_unique<SliderAttachment> (params, "octave", octave);
    holdAttach = std::make_unique<SliderAttachment> (params, "hold", hold);
    releaseAttach = std::make_unique<SliderAttachment> (params, "release", release);
    velocityAttach = std::make_unique<SliderAttachment> (params, "velocity", velocity);
    pitchBendRangeAttach = std::make_unique<SliderAttachment> (params, "pitchBendRange", pitchBendRange);
    pitchBendAttach = std::make_unique<ButtonAttachment> (params, "pitchBend", pitchBend);
    glideAttach = std::make_unique<ButtonAttachment> (params, "glide", glide);
    bypassAttach = std::make_unique<ButtonAttachment> (params, "bypass", bypass);
    paraphonicAttach = std::make_unique<ButtonAttachment> (params, "paraphonic", paraphonic);
    followKeyAttach = std::make_unique<ButtonAttachment> (params, "followKey", followKey);
    neuralPitchAttach = std::make_unique<ButtonAttachment> (params, "neuralPitch", neuralPitch);

    startTimerHz (20);
}

SattariVoxKeyAudioProcessorEditor::~SattariVoxKeyAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SattariVoxKeyAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariVoxKeyAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariVoxKeyAudioProcessorEditor::setupSlider (juce::Slider& slider, const juce::String& suffix)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 22);
    slider.setTextValueSuffix (suffix);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (sattari::brand::kAccent));
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff272c42));
    addAndMakeVisible (slider);
}

void SattariVoxKeyAudioProcessorEditor::setupCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff111525));
    combo.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour (juce::ComboBox::outlineColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (combo);
}

void SattariVoxKeyAudioProcessorEditor::setupLabel (juce::Label& label, const juce::String& text, float size)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (size, juce::Font::bold));
    label.setColour (juce::Label::textColourId, juce::Colour (0xffffd3a1));
    label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (label);
}

void SattariVoxKeyAudioProcessorEditor::setupToggle (juce::ToggleButton& button)
{
    button.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    button.setColour (juce::ToggleButton::tickColourId, juce::Colour (sattari::brand::kAccent));
    addAndMakeVisible (button);
}

void SattariVoxKeyAudioProcessorEditor::timerCallback()
{
    updateMeters();
}

void SattariVoxKeyAudioProcessorEditor::updateMeters()
{
    const auto state = audioProcessor.getDisplayState();
    orb.setState (state);
    detectedLabel.setText ("Detected: " + state.detectedName, juce::dontSendNotification);
    outputLabel.setText ("MIDI Out: " + state.outputName, juce::dontSendNotification);
    confidenceLabel.setText (state.midiActive ? "MIDI activity: sending" : (state.voiced ? "MIDI activity: tracking" : "MIDI activity: idle"), juce::dontSendNotification);

    // Grey the manual Key menu while the bus is driving the key, and show the
    // followed root in the caption, so the override is never silent.
    static const char* noteNm[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    int followedRoot = 0;
    const bool following = audioProcessor.followingBusKey (followedRoot);
    key.setEnabled (! following);
    keyLabel.setText (following ? "KEY -> " + juce::String (noteNm[followedRoot]) : "KEY",
                      juce::dontSendNotification);
}

void SattariVoxKeyAudioProcessorEditor::paint (juce::Graphics& g)
{
    lookAndFeel.paintSuiteBackground (g, getLocalBounds());
}

void SattariVoxKeyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (28);
    title.setBounds (area.removeFromTop (40));
    subtitle.setBounds (area.removeFromTop (24));

    auto presetRow = area.removeFromTop (34);
    userPresets.setBounds (presetRow.removeFromLeft (200));
    presetRow.removeFromLeft (10);
    savePresetButton.setBounds (presetRow.removeFromLeft (120));
    presetRow.removeFromLeft (8);
    abRow.setBounds (presetRow.removeFromLeft (94));
    presetRow.removeFromLeft (8);
    midiLearnButton.setBounds (presetRow.removeFromLeft (120));

    area.removeFromTop (10);

    auto left = area.removeFromLeft (500);
    area.removeFromLeft (18);
    auto right = area;

    inputLabel.setBounds (left.removeFromTop (24));
    auto comboCaptions = left.removeFromTop (13);
    modeLabel.setBounds (comboCaptions.removeFromLeft (160));
    comboCaptions.removeFromLeft (8);
    keyLabel.setBounds (comboCaptions.removeFromLeft (80));
    comboCaptions.removeFromLeft (8);
    scaleLabel.setBounds (comboCaptions.removeFromLeft (130));
    auto combos = left.removeFromTop (40);
    mode.setBounds (combos.removeFromLeft (160));
    combos.removeFromLeft (8);
    key.setBounds (combos.removeFromLeft (80));
    combos.removeFromLeft (8);
    scale.setBounds (combos.removeFromLeft (130));

    left.removeFromTop (12);
    auto cap1 = left.removeFromTop (13);
    sensitivityLabel.setBounds (cap1.removeFromLeft (100));
    smoothnessLabel.setBounds (cap1.removeFromLeft (100));
    confidenceKnobLabel.setBounds (cap1.removeFromLeft (100));
    octaveLabel.setBounds (cap1.removeFromLeft (100));
    auto row1 = left.removeFromTop (118);
    sensitivity.setBounds (row1.removeFromLeft (100));
    smoothness.setBounds (row1.removeFromLeft (100));
    confidence.setBounds (row1.removeFromLeft (100));
    octave.setBounds (row1.removeFromLeft (100));

    left.removeFromTop (10);
    auto cap2 = left.removeFromTop (13);
    holdLabel.setBounds (cap2.removeFromLeft (100));
    releaseLabel.setBounds (cap2.removeFromLeft (100));
    velocityLabel.setBounds (cap2.removeFromLeft (100));
    bendRangeLabel.setBounds (cap2.removeFromLeft (100));
    auto row2 = left.removeFromTop (118);
    hold.setBounds (row2.removeFromLeft (100));
    release.setBounds (row2.removeFromLeft (100));
    velocity.setBounds (row2.removeFromLeft (100));
    pitchBendRange.setBounds (row2.removeFromLeft (100));

    left.removeFromTop (8);
    // Five controls in the 500px column: trimmed so "Follow Song Key" fits
    // (the widths sum to 500 - previously overflowed and truncated the label).
    auto toggleRow = left.removeFromTop (32);
    pitchBend.setBounds (toggleRow.removeFromLeft (100));
    glide.setBounds (toggleRow.removeFromLeft (122));
    bypass.setBounds (toggleRow.removeFromLeft (82));
    panicButton.setBounds (toggleRow.removeFromLeft (66));
    followKey.setBounds (toggleRow.removeFromLeft (130));
   #if SATTARI_ENABLE_EXPERIMENTAL
    paraphonic.setBounds (toggleRow.removeFromLeft (160));
   #endif

    left.removeFromTop (6);
    auto toggleRow2 = left.removeFromTop (28);
    neuralPitch.setBounds (toggleRow2.removeFromLeft (150));

    left.removeFromTop (10);
    routingLabel.setBounds (left.removeFromTop (48));

    orb.setBounds (right.removeFromTop (290));
    right.removeFromTop (12);
    detectedLabel.setBounds (right.removeFromTop (30));
    outputLabel.setBounds (right.removeFromTop (30));
    confidenceLabel.setBounds (right.removeFromTop (30));
}
