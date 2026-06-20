#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr auto bg = 0xff06070b;
constexpr auto card = 0xff10131c;
constexpr auto cardHi = 0xff171b27;
constexpr auto line = 0xff30384d;
constexpr auto muted = 0xff8e98ad;
constexpr auto text = 0xfff4f6fb;
constexpr auto orange = 0xffff6b35;
constexpr auto amber = 0xffffc170;
constexpr auto cyan = 0xff35f4d3;
constexpr auto blue = 0xff6f86ff;
constexpr auto magenta = 0xffff4fa3;

juce::Colour c (uint32 rgba) { return juce::Colour (rgba); }

juce::Rectangle<float> inset (juce::Rectangle<int> r, float amount)
{
    return r.toFloat().reduced (amount);
}

void drawSoftShadow (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r.translated (0.0f, 8.0f), 20.0f);
}

void drawPill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label,
               juce::Colour fill, juce::Colour outline, juce::Colour labelColour = c (text))
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (outline);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (labelColour);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawFittedText (label, r.toNearestInt(), juce::Justification::centred, 1);
}

juce::Path roundedArc (juce::Rectangle<float> bounds, float startAngle, float endAngle)
{
    juce::Path p;
    p.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), bounds.getWidth() * 0.5f, bounds.getHeight() * 0.5f,
                     0.0f, startAngle, endAngle, true);
    return p;
}
}

SattariAutoPitchLookAndFeel::SattariAutoPitchLookAndFeel()
{
    setColour (juce::Slider::textBoxBackgroundColourId, c (0xff080a11));
    setColour (juce::Slider::textBoxOutlineColourId, c (0xff2a3245));
    setColour (juce::Slider::textBoxTextColourId, c (text));
    setColour (juce::ComboBox::textColourId, c (text));
    setColour (juce::ComboBox::backgroundColourId, c (0xff121622));
    setColour (juce::ComboBox::outlineColourId, c (line));
    setColour (juce::PopupMenu::backgroundColourId, c (0xff11141d));
    setColour (juce::PopupMenu::textColourId, c (text));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c (orange));
    setColour (juce::TextButton::buttonColourId, c (orange));
    setColour (juce::TextButton::textColourOffId, c (text));
}

juce::Font SattariAutoPitchLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::FontOptions (15.0f, juce::Font::bold);
}

void SattariAutoPitchLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                                                    juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (8.0f);
    auto dial = bounds.withSizeKeepingCentre (juce::jmin (bounds.getWidth(), bounds.getHeight() - 24.0f),
                                              juce::jmin (bounds.getWidth(), bounds.getHeight() - 24.0f));
    dial.translate (0.0f, -5.0f);
    const auto centre = dial.getCentre();
    const float radius = juce::jmin (dial.getWidth(), dial.getHeight()) * 0.5f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const bool utility = slider.getName().containsIgnoreCase ("gain");
    const bool confidence = slider.getName().containsIgnoreCase ("confidence");
    const auto accent = confidence ? c (cyan) : utility ? c (blue) : c (orange);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (dial.expanded (2.0f).translated (0.0f, 5.0f));

    g.setGradientFill (juce::ColourGradient (c (0xff1b2030), dial.getTopLeft(), c (0xff080a10), dial.getBottomRight(), false));
    g.fillEllipse (dial);
    g.setColour (c (0xff313a52));
    g.drawEllipse (dial, 1.4f);

    auto track = dial.reduced (8.0f);
    g.setColour (c (0xff252b3b));
    g.strokePath (roundedArc (track, rotaryStartAngle, rotaryEndAngle), juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (accent);
    g.strokePath (roundedArc (track, rotaryStartAngle, angle), juce::PathStrokeType (5.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto pointerLength = radius * 0.57f;
    juce::Path pointer;
    pointer.addRoundedRectangle (-2.0f, -pointerLength, 4.0f, pointerLength * 0.65f, 2.0f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (c (text));
    g.fillPath (pointer);

    g.setColour (accent.withAlpha (0.18f));
    g.fillEllipse (dial.reduced (18.0f));
    g.setColour (accent.withAlpha (0.75f));
    g.drawEllipse (dial.reduced (18.0f), 1.0f);
}

void SattariAutoPitchLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                                    bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (1.0f, 3.0f);
    const bool on = button.getToggleState();
    const auto accent = button.getButtonText().containsIgnoreCase ("harm") ? c (cyan) : c (orange);

    g.setColour (on ? accent.withAlpha (0.16f) : c (0xff111622));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (on ? accent.withAlpha (0.88f) : c (line).withAlpha (highlighted || down ? 0.95f : 0.65f));
    g.drawRoundedRectangle (r, 10.0f, 1.2f);

    auto led = r.removeFromLeft (24.0f).reduced (7.0f);
    g.setColour (on ? accent : c (0xff455066));
    g.fillEllipse (led);
    g.setColour (on ? juce::Colours::white.withAlpha (0.55f) : juce::Colours::transparentBlack);
    g.fillEllipse (led.reduced (3.0f));

    g.setColour (on ? c (text) : c (muted));
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawFittedText (button.getButtonText(), r.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1);
}

void SattariAutoPitchLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                                int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (c (0xff1b2130), r.getTopLeft(), c (0xff0d1018), r.getBottomRight(), false));
    g.fillRoundedRectangle (r, 11.0f);
    g.setColour (isButtonDown ? c (orange) : c (line));
    g.drawRoundedRectangle (r, 11.0f, 1.2f);

    juce::Path chevron;
    chevron.startNewSubPath ((float) width - 20.0f, (float) height * 0.43f);
    chevron.lineTo ((float) width - 14.0f, (float) height * 0.58f);
    chevron.lineTo ((float) width - 8.0f, (float) height * 0.43f);
    g.setColour (c (orange));
    g.strokePath (chevron, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SattariAutoPitchLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button&, const juce::Colour&,
                                                        bool highlighted, bool down)
{
    auto r = g.getClipBounds().toFloat().reduced (0.5f);
    const auto top = down ? c (0xffbd411d) : highlighted ? c (0xffff865b) : c (orange);
    const auto bottom = down ? c (0xff742913) : c (0xffa63b1c);
    g.setGradientFill (juce::ColourGradient (top, r.getTopLeft(), bottom, r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (r.reduced (1.0f), 11.0f, 1.0f);
}

void SattariAutoPitchLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                                  bool, bool)
{
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.setColour (c (text));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 0), juce::Justification::centred, 1);
}

void PitchCompassStub::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (2.0f);
    const auto centre = area.getCentre();
    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.39f;

    g.setColour (c (0xff080a10));
    g.fillRoundedRectangle (area, 18.0f);

    g.setGradientFill (juce::ColourGradient (c (0xff181f31), area.getTopLeft(), c (0xff0b0d13), area.getBottomRight(), false));
    g.fillEllipse (area.withSizeKeepingCentre (radius * 2.25f, radius * 2.25f));
    g.setColour (c (line));
    g.drawEllipse (area.withSizeKeepingCentre (radius * 2.25f, radius * 2.25f), 1.0f);

    const juce::StringArray notes { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const std::array<int, 7> highlighted { 9, 11, 0, 2, 4, 5, 7 };

    for (int i = 0; i < notes.size(); ++i)
    {
        const float angle = juce::MathConstants<float>::twoPi * ((float) i / (float) notes.size()) - juce::MathConstants<float>::halfPi;
        const auto x = centre.x + std::cos (angle) * radius;
        const auto y = centre.y + std::sin (angle) * radius;
        juce::Rectangle<float> noteBounds (x - 18.0f, y - 11.0f, 36.0f, 22.0f);
        const bool inScale = std::find (highlighted.begin(), highlighted.end(), i) != highlighted.end();
        g.setColour (inScale ? c (orange).withAlpha (0.24f) : c (0xff151a25));
        g.fillRoundedRectangle (noteBounds, 8.0f);
        g.setColour (inScale ? c (amber) : c (0xff626b80));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (notes[i], noteBounds.toNearestInt(), juce::Justification::centred, 1);
    }

    g.setColour (c (cyan));
    juce::Path needle;
    needle.startNewSubPath (centre.x, centre.y - radius * 0.72f);
    needle.lineTo (centre.x + 6.0f, centre.y + 8.0f);
    needle.lineTo (centre.x - 6.0f, centre.y + 8.0f);
    needle.closeSubPath();
    g.fillPath (needle);

    g.setColour (c (text));
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawFittedText ("A", juce::Rectangle<float> (centre.x - 32.0f, centre.y - 30.0f, 64.0f, 34.0f).toNearestInt(), juce::Justification::centred, 1);
    g.setColour (c (muted));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawFittedText ("MINOR • SCALE LOCK", juce::Rectangle<float> (centre.x - 70.0f, centre.y + 6.0f, 140.0f, 20.0f).toNearestInt(), juce::Justification::centred, 1);

    drawPill (g, area.removeFromBottom (28.0f).withSizeKeepingCentre (150.0f, 24.0f), "PITCH COMPASS", c (0xff111723), c (cyan).withAlpha (0.5f), c (cyan));
}

SattariAutoPitchAudioProcessorEditor::SattariAutoPitchAudioProcessorEditor (SattariAutoPitchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
      presetManager (p.parameters(), "auto-pitch")
{
    setSize (980, 690);
    setLookAndFeel (&lookAndFeel);

    title.setText ("SATTARI AUTO PITCH", juce::dontSendNotification);
    title.setFont (juce::FontOptions (31.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, c (text));
    addAndMakeVisible (title);

    subtitle.setText ("Premium realtime vocal tuning • original Sattari performance skin", juce::dontSendNotification);
    subtitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    subtitle.setColour (juce::Label::textColourId, c (muted));
    addAndMakeVisible (subtitle);

    setupCombo (keyRoot);
    keyRoot.addItemList (juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    setupCombo (scale);
    scale.addItemList (juce::StringArray { "major", "minor", "chromatic" }, 1);
    setupCombo (style);
    style.addItemList (juce::StringArray { "Natural", "Classic", "Modern Rap", "Robot", "Big Harmony" }, 1);

    setupSlider (amount, "%");
    amount.setName ("amount");
    setupSlider (speed, "%");
    speed.setName ("speed");
    setupSlider (inputGain, " dB");
    inputGain.setName ("input gain");
    setupSlider (outputGain, " dB");
    outputGain.setName ("output gain");
    setupSlider (traceConfidence, "%");
    traceConfidence.setName ("confidence");
    traceConfidence.setEnabled (false);
    setupSlider (formantShift, " st");
    formantShift.setName ("formant shift");

    amount.textFromValueFunction = [] (double value) { return juce::String ((int) std::round (value * 100.0)) + "%"; };
    speed.textFromValueFunction = [] (double value) { return juce::String ((int) std::round (value * 100.0)) + "%"; };
    traceConfidence.textFromValueFunction = [] (double value) { return juce::String ((int) std::round (value * 100.0)) + "%"; };

    enableCorrection.setButtonText ("Correction On");
    addAndMakeVisible (enableCorrection);

    autoKey.setButtonText ("Auto Key Follow");
    autoKey.setTooltip ("Reserved alpha control: current build uses imported/offline trace keys or the selected key.");
    addAndMakeVisible (autoKey);

    doNoHarm.setButtonText ("Do No Harm");
    addAndMakeVisible (doNoHarm);

    formantPreserve.setButtonText ("Formant Preserve");
    addAndMakeVisible (formantPreserve);

    loadTraceButton.onClick = [this] { loadTraceFromClipboard(); };
    addAndMakeVisible (loadTraceButton);

    // Factory preset bank (top bar) on top of the shared user-preset system.
    registerFactoryPresets();
    setupCombo (factoryPresets);
    factoryPresets.setTextWhenNothingSelected ("Factory presets...");
    factoryPresets.addItemList (presetManager.factoryPresetNames(), 1);
    factoryPresets.onChange = [this]
    {
        const int idx = factoryPresets.getSelectedItemIndex();
        if (idx >= 0) presetManager.loadFactoryPreset (idx);
    };
    addAndMakeVisible (factoryPresets);

    // Shared cross-plugin preset system (top bar; styled by this plugin's own look).
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
    refreshUserPresets();

    setupLabel (keyLabel, "INPUT / KEY", 12.0f);
    setupLabel (correctionLabel, "TUNE ENGINE", 12.0f);
    setupLabel (gainLabel, "GAIN / SAFETY", 12.0f);
    setupLabel (traceLabel, "ANALYSIS", 12.0f);
    setupCaption (amountCaption, "AMOUNT");
    setupCaption (speedCaption, "RETUNE SPEED");
    setupCaption (confidenceCaption, "TRACE CONF");
    setupCaption (inputCaption, "INPUT");
    setupCaption (outputCaption, "OUTPUT");
    setupCaption (formantCaption, "FORMANT");

    traceStatus.setColour (juce::Label::textColourId, c (muted));
    traceStatus.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    traceStatus.setJustificationType (juce::Justification::centredLeft);
    traceStatus.setText (audioProcessor.getLiveStatusSummary(), juce::dontSendNotification);
    addAndMakeVisible (traceStatus);

    keyDetectionStatus.setColour (juce::Label::textColourId, c (cyan));
    keyDetectionStatus.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    keyDetectionStatus.setJustificationType (juce::Justification::centredLeft);
    keyDetectionStatus.setText (audioProcessor.getKeyDetectionTabSummary(), juce::dontSendNotification);
    keyDetectionStatus.setTooltip ("Key Detection tab shares the Sattari Harmonic Lab core: Sattari Wheel, confidence, compatible moves, and Send to Tune state.");
    addAndMakeVisible (keyDetectionStatus);
    addAndMakeVisible (pitchCompass);

    auto& params = audioProcessor.parameters();
    keyRootAttach = std::make_unique<ComboAttachment> (params, "keyRoot", keyRoot);
    scaleAttach = std::make_unique<ComboAttachment> (params, "scale", scale);
    styleAttach = std::make_unique<ComboAttachment> (params, "style", style);
    amountAttach = std::make_unique<SliderAttachment> (params, "amount", amount);
    speedAttach = std::make_unique<SliderAttachment> (params, "speed", speed);
    inputGainAttach = std::make_unique<SliderAttachment> (params, "inputGain", inputGain);
    outputGainAttach = std::make_unique<SliderAttachment> (params, "outputGain", outputGain);
    traceConfidenceAttach = std::make_unique<SliderAttachment> (params, "traceConfidence", traceConfidence);
    formantShiftAttach = std::make_unique<SliderAttachment> (params, "formantShift", formantShift);
    doNoHarmAttach = std::make_unique<ButtonAttachment> (params, "doNoHarm", doNoHarm);
    enableCorrectionAttach = std::make_unique<ButtonAttachment> (params, "enableCorrection", enableCorrection);
    autoKeyAttach = std::make_unique<ButtonAttachment> (params, "autoKey", autoKey);
    formantPreserveAttach = std::make_unique<ButtonAttachment> (params, "formantPreserve", formantPreserve);

    startTimerHz (4);
}

SattariAutoPitchAudioProcessorEditor::~SattariAutoPitchAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SattariAutoPitchAudioProcessorEditor::registerFactoryPresets()
{
    using PV = sattari::presets::SattariPresetManager::PresetValues;
    // style: 0 Natural, 1 Classic, 2 Modern Rap, 3 Robot, 4 Big Harmony.
    presetManager.addFactoryPreset ("Natural", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.6f},{"speed",0.4f},{"doNoHarm",1},{"formantPreserve",1},{"formantShift",0} });
    presetManager.addFactoryPreset ("Hard Tune", PV {
        {"style",1},{"enableCorrection",1},{"amount",1.0f},{"speed",1.0f},{"doNoHarm",0},{"formantPreserve",1},{"formantShift",0} });
    presetManager.addFactoryPreset ("Modern Rap", PV {
        {"style",2},{"enableCorrection",1},{"amount",0.95f},{"speed",0.9f},{"doNoHarm",0},{"formantPreserve",1},{"formantShift",0} });
    presetManager.addFactoryPreset ("Robot", PV {
        {"style",3},{"enableCorrection",1},{"amount",1.0f},{"speed",1.0f},{"doNoHarm",0},{"formantPreserve",0},{"formantShift",0} });
    presetManager.addFactoryPreset ("Deeper Throat", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.7f},{"speed",0.5f},{"doNoHarm",1},{"formantPreserve",1},{"formantShift",-3} });
    presetManager.addFactoryPreset ("Bright Lift", PV {
        {"style",0},{"enableCorrection",1},{"amount",0.7f},{"speed",0.5f},{"doNoHarm",1},{"formantPreserve",1},{"formantShift",4} });
}

void SattariAutoPitchAudioProcessorEditor::setupSlider (juce::Slider& s, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.18f, juce::MathConstants<float>::pi * 2.82f, true);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 22);
    s.setTextValueSuffix (suffix);
    s.setColour (juce::Slider::thumbColourId, c (text));
    s.setColour (juce::Slider::textBoxTextColourId, c (text));
    s.setColour (juce::Slider::textBoxBackgroundColourId, c (0xff080a11));
    s.setColour (juce::Slider::textBoxOutlineColourId, c (0xff2a3245));
    addAndMakeVisible (s);
}

void SattariAutoPitchAudioProcessorEditor::setupCombo (juce::ComboBox& combo)
{
    combo.setColour (juce::ComboBox::backgroundColourId, c (0xff151a25));
    combo.setColour (juce::ComboBox::textColourId, c (text));
    combo.setColour (juce::ComboBox::outlineColourId, c (line));
    addAndMakeVisible (combo);
}

void SattariAutoPitchAudioProcessorEditor::setupLabel (juce::Label& label, const juce::String& labelText, float size)
{
    label.setText (labelText, juce::dontSendNotification);
    label.setFont (juce::FontOptions (size, juce::Font::bold));
    label.setColour (juce::Label::textColourId, c (amber));
    addAndMakeVisible (label);
}

void SattariAutoPitchAudioProcessorEditor::setupCaption (juce::Label& label, const juce::String& labelText)
{
    label.setText (labelText, juce::dontSendNotification);
    label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, c (muted));
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);
}

void SattariAutoPitchAudioProcessorEditor::loadTraceFromClipboard()
{
    const auto traceJson = juce::SystemClipboard::getTextFromClipboard();
    const bool ok = audioProcessor.applyTraceContractJson (traceJson);
    traceStatus.setColour (juce::Label::textColourId, ok ? juce::Colours::lightgreen : juce::Colours::orange);
    traceStatus.setText (audioProcessor.getLastTraceSummary(), juce::sendNotificationSync);
}

void SattariAutoPitchAudioProcessorEditor::refreshUserPresets()
{
    userPresets.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& name : presetManager.userPresetNames())
        userPresets.addItem (name, id++);
}

void SattariAutoPitchAudioProcessorEditor::saveCurrentAsUserPreset()
{
    const auto name = "User " + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    if (presetManager.saveUserPreset (name))
    {
        refreshUserPresets();
        userPresets.setText (name, juce::dontSendNotification);
    }
}

void SattariAutoPitchAudioProcessorEditor::timerCallback()
{
    traceStatus.setText (audioProcessor.getLiveStatusSummary(), juce::dontSendNotification);
    keyDetectionStatus.setText (audioProcessor.getKeyDetectionTabSummary(), juce::dontSendNotification);
}

void SattariAutoPitchAudioProcessorEditor::drawCard (juce::Graphics& g, juce::Rectangle<float> r,
                                                     const juce::String& eyebrow, const juce::String& titleText,
                                                     juce::Colour accent) const
{
    drawSoftShadow (g, r);
    g.setGradientFill (juce::ColourGradient (c (cardHi), r.getTopLeft(), c (card), r.getBottomRight(), false));
    g.fillRoundedRectangle (r, 20.0f);
    g.setColour (c (line).withAlpha (0.78f));
    g.drawRoundedRectangle (r, 20.0f, 1.0f);
    g.setColour (accent.withAlpha (0.75f));
    g.fillRoundedRectangle (r.removeFromTop (3.0f), 2.0f);

    auto textArea = r.reduced (18.0f, 12.0f);
    g.setColour (accent);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawFittedText (eyebrow, textArea.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    g.setColour (c (text));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawFittedText (titleText, textArea.removeFromTop (24.0f).toNearestInt(), juce::Justification::centredLeft, 1);
}

void SattariAutoPitchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (c (bg));
    auto full = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (c (0xff160b16), full.getTopLeft(), c (0xff05131b), full.getBottomRight(), false));
    g.fillRect (full);

    auto glow = full.reduced (28.0f).withHeight (170.0f);
    g.setGradientFill (juce::ColourGradient (c (orange).withAlpha (0.18f), glow.getTopLeft(), c (cyan).withAlpha (0.10f), glow.getTopRight(), false));
    g.fillRoundedRectangle (glow, 28.0f);

    drawPill (g, headerArea.toFloat().withTrimmedLeft (650.0f).withTrimmedRight (18.0f).withHeight (27.0f).translated (0.0f, 8.0f),
              "LOW LATENCY • ALPHA", c (0xff121826), c (cyan).withAlpha (0.65f), c (cyan));
    drawPill (g, headerArea.toFloat().withTrimmedLeft (655.0f).withTrimmedRight (150.0f).withTop (45.0f).withHeight (25.0f),
              "AUTO", c (orange).withAlpha (0.20f), c (orange).withAlpha (0.75f), c (amber));
    drawPill (g, headerArea.toFloat().withTrimmedLeft (735.0f).withTrimmedRight (82.0f).withTop (45.0f).withHeight (25.0f),
              "GRAPH", c (0xff101520), c (line), c (muted));
    drawPill (g, headerArea.toFloat().withTrimmedLeft (820.0f).withTrimmedRight (4.0f).withTop (45.0f).withHeight (25.0f),
              "ADVANCED", c (0xff101520), c (line), c (muted));

    drawCard (g, inset (keyCard, 0.0f), "01", "Input & Key", c (orange));
    drawCard (g, inset (tuneCard, 0.0f), "02", "Tune Engine", c (cyan));
    drawCard (g, inset (compassCard, 0.0f), "03", "Pitch View", c (blue));
    drawCard (g, inset (utilityCard, 0.0f), "04", "Utility", c (magenta));
    drawCard (g, inset (statusCard, 0.0f), "LIVE", "Trace / Harmonic Lab", c (amber));

    auto keyHint = keyCard.toFloat().reduced (20.0f).removeFromBottom (40.0f);
    drawPill (g, keyHint.withWidth (92.0f), "ROOT", c (0xff0c1019), c (line), c (muted));
    drawPill (g, keyHint.withX (keyHint.getX() + 104.0f).withWidth (92.0f), "SCALE", c (0xff0c1019), c (line), c (muted));
    drawPill (g, keyHint.withX (keyHint.getX() + 208.0f).withWidth (118.0f), "STYLE", c (0xff0c1019), c (line), c (muted));
}

void SattariAutoPitchAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);

    auto presetBar = area.removeFromTop (34);
    factoryPresets.setBounds (presetBar.removeFromLeft (210));
    presetBar.removeFromLeft (10);
    userPresets.setBounds (presetBar.removeFromLeft (210));
    presetBar.removeFromLeft (10);
    savePresetButton.setBounds (presetBar.removeFromLeft (140));
    area.removeFromTop (12);

    headerArea = area.removeFromTop (78);
    auto headerContent = headerArea;
    title.setBounds (headerContent.removeFromLeft (520).withTrimmedTop (2).withHeight (36));
    subtitle.setBounds (title.getBounds().translated (0, 34).withHeight (22));

    auto top = area.removeFromTop (250);
    keyCard = top.removeFromLeft (345);
    top.removeFromLeft (16);
    tuneCard = top.removeFromLeft (330);
    top.removeFromLeft (16);
    compassCard = top;

    area.removeFromTop (16);
    auto bottom = area;
    utilityCard = bottom.removeFromLeft (425);
    bottom.removeFromLeft (16);
    statusCard = bottom;

    auto keyContent = keyCard.reduced (22, 18);
    keyContent.removeFromTop (45);
    keyLabel.setBounds (keyContent.removeFromTop (18));
    keyContent.removeFromTop (8);
    auto comboRow = keyContent.removeFromTop (42);
    keyRoot.setBounds (comboRow.removeFromLeft (78));
    comboRow.removeFromLeft (10);
    scale.setBounds (comboRow.removeFromLeft (106));
    comboRow.removeFromLeft (10);
    style.setBounds (comboRow);
    keyContent.removeFromTop (18);
    enableCorrection.setBounds (keyContent.removeFromTop (32));
    keyContent.removeFromTop (8);
    autoKey.setBounds (keyContent.removeFromTop (32));
    keyContent.removeFromTop (8);
    doNoHarm.setBounds (keyContent.removeFromTop (32));

    auto tuneContent = tuneCard.reduced (22, 18);
    tuneContent.removeFromTop (48);
    correctionLabel.setBounds (tuneContent.removeFromTop (18));
    tuneContent.removeFromTop (8);
    auto bigRow = tuneContent.removeFromTop (135);
    auto knobW = bigRow.getWidth() / 2;
    amount.setBounds (bigRow.removeFromLeft (knobW).reduced (3, 0));
    speed.setBounds (bigRow.reduced (3, 0));
    amountCaption.setBounds (amount.getBounds().withY (amount.getBottom() - 2).withHeight (18));
    speedCaption.setBounds (speed.getBounds().withY (speed.getBottom() - 2).withHeight (18));

    auto compassContent = compassCard.reduced (20, 18);
    compassContent.removeFromTop (48);
    traceLabel.setBounds (compassContent.removeFromTop (18));
    compassContent.removeFromTop (8);
    pitchCompass.setBounds (compassContent);

    auto utilityContent = utilityCard.reduced (22, 18);
    utilityContent.removeFromTop (48);
    gainLabel.setBounds (utilityContent.removeFromTop (18));
    utilityContent.removeFromTop (8);
    auto utilityRow = utilityContent.removeFromTop (130);
    auto uW = utilityRow.getWidth() / 4;
    inputGain.setBounds (utilityRow.removeFromLeft (uW).reduced (2, 0));
    outputGain.setBounds (utilityRow.removeFromLeft (uW).reduced (2, 0));
    traceConfidence.setBounds (utilityRow.removeFromLeft (uW).reduced (2, 0));
    formantShift.setBounds (utilityRow.reduced (2, 0));
    inputCaption.setBounds (inputGain.getBounds().withY (inputGain.getBottom() - 2).withHeight (18));
    outputCaption.setBounds (outputGain.getBounds().withY (outputGain.getBottom() - 2).withHeight (18));
    confidenceCaption.setBounds (traceConfidence.getBounds().withY (traceConfidence.getBottom() - 2).withHeight (18));
    formantCaption.setBounds (formantShift.getBounds().withY (formantShift.getBottom() - 2).withHeight (18));
    utilityContent.removeFromTop (4);
    formantPreserve.setBounds (utilityContent.removeFromTop (30));

    auto statusContent = statusCard.reduced (22, 18);
    statusContent.removeFromTop (48);
    loadTraceButton.setBounds (statusContent.removeFromTop (38));
    statusContent.removeFromTop (12);
    traceStatus.setBounds (statusContent.removeFromTop (52));
    statusContent.removeFromTop (4);
    keyDetectionStatus.setBounds (statusContent.removeFromTop (66));
}
