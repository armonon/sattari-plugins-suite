#include "RoyalChainPanels.h"
#include "RoyalChainProcessor.h"
#include "MacroController.h"
#include "CymaticPlate.h"

#include <cmath>

namespace th = rc::theme;

namespace
{
inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float rawValue (juce::AudioProcessorValueTreeState& apvts, const char* id, float fallback = 0.0f)
{
    if (auto* p = apvts.getRawParameterValue (id))
        return p->load (std::memory_order_relaxed);
    return fallback;
}

inline float gainToDb (float g) noexcept
{
    return g > 0.0f ? std::max (-100.0f, 20.0f * std::log10 (g)) : -100.0f;
}

juce::String signedOneDecimal (float v)
{
    if (std::abs (v) < 0.05f)
        return "0.0";
    return (v > 0.0f ? juce::String ("+") : th::utf8 ("\xe2\x88\x92")) + juce::String (std::abs (v), 1);
}

void drawString (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r,
                 const juce::FontOptions& f, juce::Colour c, juce::Justification j)
{
    g.setColour (c);
    g.setFont (juce::Font (f));
    g.drawText (t, r, j, false);
}
} // namespace

//==============================================================================
namespace rc
{
juce::String formatMacroValue (const theme::MacroSpec& spec, double v, bool withUnit)
{
    if (spec.isDb)
        return signedOneDecimal ((float) v) + (withUnit ? " dB" : "");
    return juce::String (juce::roundToInt (v * 100.0)) + (withUnit ? "%" : "");
}

void configurePortalSlider (juce::Slider& s, const theme::MacroSpec& spec, juce::AudioProcessorValueTreeState& apvts)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 18);
    juce::String name (spec.caption);
    if (auto* p = apvts.getParameter (spec.paramId))
        name = p->getName (64);
    s.setName (name);
    s.setTitle (name);
    s.setDescription (spec.description);
    s.setTooltip (name + theme::utf8 (" \xe2\x80\x94 ") + spec.description + " Double-click resets.");
    s.setPopupDisplayEnabled (false, false, nullptr);
    s.getProperties().set (theme::accentProperty, (juce::int64) spec.colour);
}

// The attachment installs its own text functions, so ours go on afterwards.
static void installTextFunctions (juce::Slider& s, const theme::MacroSpec& spec)
{
    const auto* sp = &spec;
    s.textFromValueFunction = [sp] (double v) { return formatMacroValue (*sp, v, true); };
    s.valueFromTextFunction = [sp] (const juce::String& text)
    {
        const auto cleaned = text.replace (theme::utf8 ("\xe2\x88\x92"), "-").retainCharacters ("0123456789.-+");
        const double v = cleaned.getDoubleValue();
        return sp->isDb ? v : v / 100.0;
    };
    s.updateText();
}
} // namespace rc

//==============================================================================
//  ModuleDeck
//==============================================================================
const ModuleDeck::ChipSpec ModuleDeck::kChips[10] = {
    { "IN",      0,  0, th::knobTrim,         -1 },
    { "CLEAN",   1,  1, th::knobNeutral,      -1 },
    { "TUNE",    2,  2, th::kNodes[0].colour,  0 },
    { "CONTROL", 3,  3, th::kNodes[1].colour,  1 },
    { "TONE",    4,  5, th::knobNeutral,      -1 },
    { "DE-ESS",  6,  6, th::kNodes[2].colour,  2 },
    { "COLOR",   7,  7, th::kNodes[3].colour,  3 },
    { "WIDTH",   8,  8, th::kNodes[4].colour,  4 },
    { "SPACE",   9,  9, th::knobNeutral,      -1 },
    { "FINISH", 10, 10, th::knobTrim,         -1 },
};

int ModuleDeck::unitX (int unit) noexcept
{
    return 12 + juce::roundToInt ((double) unit * 1044.0 / (double) kUnits);
}

ModuleDeck::ModuleDeck (RoyalChainAudioProcessor& p)
    : processor (p)
{
    setOpaque (true);
    setTitle ("Signal chain");
    setDescription ("Every stage of the Royal Chain in signal-flow order with its macro knob.");

    auto& apvts = processor.parameters();
    for (int i = 0; i < RoyalChainAudioProcessor::stageCount; ++i)
    {
        auto& button = bypassButtons[(size_t) i];
        button = std::make_unique<sattari::releaseui::IconButton> (sattari::releaseui::Icon::power,
            juce::String (RoyalChainAudioProcessor::stageName (i)) + " power");
        button->setLookAndFeel (&bypassLook);
        button->onClick = [this, i]
        {
            auto* parameter = processor.parameters().getParameter (RoyalChainAudioProcessor::stageBypassId (i));
            parameter->beginChangeGesture(); parameter->setValueNotifyingHost (parameter->getValue() < .5f ? 1.0f : 0.0f); parameter->endChangeGesture();
        };
        addAndMakeVisible (*button);
    }
    for (int m = 0; m < kUnits; ++m)
    {
        const auto& spec = th::kMacros[m];
        auto& s = knobs[(size_t) m];
        rc::configurePortalSlider (s, spec, apvts);
        addAndMakeVisible (s);
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.paramId, s));
        rc::installTextFunctions (s, spec);
        s.addMouseListener (this, false);
        s.onDragStart = [this, id = spec.id]
        {
            if (onFocusMacro)
                onFocusMacro (id);
        };
    }
}

ModuleDeck::~ModuleDeck()
{
    for (auto& button : bypassButtons) button->setLookAndFeel (nullptr);
    for (auto& s : knobs)
        s.removeMouseListener (this);
}

juce::Rectangle<int> ModuleDeck::chipRect (int chip) const noexcept
{
    const auto& c = kChips[chip];
    const int x0 = unitX (c.unit0) + 3, x1 = unitX (c.unit1 + 1) - 3;
    return { x0, 10, x1 - x0, 26 };
}

float ModuleDeck::chipActivity (int chip) const noexcept
{
    auto& apvts = processor.parameters();
    const float in = processor.getInputMeter(), out = processor.getOutputMeter();
    switch (chip)
    {
        case 0: return clamp01 (in);
        case 1: return clamp01 (in * rawValue (apvts, "clean"));
        case 2: { const float conf = processor.getTuneConfidence(); return clamp01 (rawValue (apvts, "tune") > 0.001f ? conf : 0.25f * conf); }
        case 3: return clamp01 (processor.getControlMeter());
        case 4: return clamp01 (out * std::max (rawValue (apvts, "tone"), rawValue (apvts, "air")));
        case 5: return clamp01 (processor.getSibilanceMeter());
        case 6: return clamp01 (out * rawValue (apvts, "glow"));
        case 7: return clamp01 (processor.getWidthMeter());
        case 8: return clamp01 (out * rawValue (apvts, "space"));
        case 9: return clamp01 (std::max (processor.getLimiterMeter(), 0.5f * out));
        default: return 0.0f;
    }
}

bool ModuleDeck::chipDimmed (int chip) const noexcept
{
    return chip > 0 && processor.parameters().getRawParameterValue (RoyalChainAudioProcessor::stageBypassId (chip - 1))->load() >= .5f;
}

void ModuleDeck::refresh()
{
    for (int i = 0; i < RoyalChainAudioProcessor::stageCount; ++i)
        bypassButtons[(size_t) i]->setToggleState (processor.parameters().getRawParameterValue (RoyalChainAudioProcessor::stageBypassId (i))->load() < .5f, juce::dontSendNotification);
    const float k = 1.0f - std::exp (-(1.0f / 30.0f) / 0.120f);
    for (int c = 0; c < 10; ++c)
    {
        led[(size_t) c] += (chipActivity (c) - led[(size_t) c]) * k;
        const bool dim = chipDimmed (c);
        if (std::abs (led[(size_t) c] - ledShown[(size_t) c]) > 0.02f || dim != dimShown[(size_t) c])
        {
            ledShown[(size_t) c] = led[(size_t) c];
            dimShown[(size_t) c] = dim;
            repaint (chipRect (c));
        }
    }
}

void ModuleDeck::setHighlightedMacro (th::MacroId id)
{
    const int u = (int) id;
    if (u == highlighted)
        return;
    auto captionRect = [] (int unit) { return juce::Rectangle<int> (unitX (unit), 40, unitX (unit + 1) - unitX (unit), 18); };
    if (juce::isPositiveAndBelow (highlighted, kUnits)) repaint (captionRect (highlighted));
    highlighted = u;
    if (juce::isPositiveAndBelow (highlighted, kUnits)) repaint (captionRect (highlighted));
}

void ModuleDeck::mouseEnter (const juce::MouseEvent& e)
{
    for (int m = 0; m < kUnits; ++m)
        if (e.eventComponent == &knobs[(size_t) m])
        {
            if (onFocusMacro)
                onFocusMacro (th::kMacros[m].id);
            return;
        }
}

void ModuleDeck::mouseExit (const juce::MouseEvent& e)
{
    for (auto& s : knobs)
        if (e.eventComponent == &s)
        {
            if (onHoverEnd)
                onHoverEnd();
            return;
        }
}

void ModuleDeck::resized()
{
    for (int i = 0; i < RoyalChainAudioProcessor::stageCount; ++i)
        bypassButtons[(size_t) i]->setBounds (chipRect (i + 1).removeFromRight (22).reduced (1));
    for (int u = 0; u < kUnits; ++u)
    {
        const int cx = (unitX (u) + unitX (u + 1)) / 2;
        knobs[(size_t) u].setBounds (cx - 38, 58, 76, 92);
    }
}

void ModuleDeck::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.fillAll (th::col (th::canvas));
    g.setColour (th::col (th::card));
    g.fillRoundedRectangle (b, 16.0f);
    g.setColour (th::col (th::cardEdge));
    g.drawRoundedRectangle (b.reduced (0.5f), 16.0f, 1.0f);

    // Signal-flow chips.
    for (int c = 0; c < 10; ++c)
    {
        const auto r = chipRect (c).toFloat();
        const auto& spec = kChips[c];
        g.setColour (th::col (th::surfaceHigh));
        g.fillRoundedRectangle (r, 7.0f);

        const float cy = r.getCentreY();
        g.setColour (th::col (spec.colour).withAlpha (0.25f + 0.75f * ledShown[(size_t) c]));
        g.fillEllipse (r.getX() + 9.0f - 2.5f, cy - 2.5f, 5.0f, 5.0f);

        drawString (g, spec.name, { r.getX() + 18.0f, r.getY(), r.getWidth() - (c > 0 ? 42.0f : 22.0f), r.getHeight() },
                    th::caps (9.0f), th::col (dimShown[(size_t) c] ? th::textFaint : th::text),
                    juce::Justification::centredLeft);

        if (spec.node >= 0)
        {
            g.setColour (th::col (spec.colour).withAlpha (0.9f));
            g.fillRoundedRectangle (r.getX() + 9.0f, r.getBottom() - 2.0f, r.getWidth() - 18.0f, 2.0f, 1.0f);
        }

        if (c < 9)
        {
            const float gx = r.getRight() + 3.0f;
            juce::Path chev;
            chev.startNewSubPath (gx - 1.5f, cy - 3.0f);
            chev.lineTo (gx + 1.5f, cy);
            chev.lineTo (gx - 1.5f, cy + 3.0f);
            g.setColour (th::col (th::hairline).brighter (0.35f));
            g.strokePath (chev, juce::PathStrokeType (1.0f));
        }
    }

    // Knob captions (node macros in their node colour).
    for (int u = 0; u < kUnits; ++u)
    {
        const auto& spec = th::kMacros[u];
        const auto rect = juce::Rectangle<float> ((float) unitX (u), 44.0f, (float) (unitX (u + 1) - unitX (u)), 12.0f);
        if (u == highlighted)
        {
            const float w = juce::GlyphArrangement::getStringWidth (juce::Font (th::caps (9.5f)), spec.caption) + 14.0f;
            g.setColour (th::col (th::pill));
            g.fillRoundedRectangle (juce::Rectangle<float> (w, 16.0f).withCentre (rect.getCentre()), 8.0f);
        }
        drawString (g, spec.caption, rect, th::caps (9.5f),
                    spec.node >= 0 ? th::col (spec.colour) : th::col (u == highlighted ? th::text : th::textDim),
                    juce::Justification::centred);
    }
}

void ModuleDeck::appendCaptionChecks (std::vector<th::CaptionCheck>& out) const
{
    for (int c = 0; c < 10; ++c)
        out.push_back ({ "deck chip", kChips[c].name, th::caps (9.0f), (float) chipRect (c).getWidth() - 22.0f });
    for (int u = 0; u < kUnits; ++u)
        out.push_back ({ "deck caption", th::kMacros[u].caption, th::caps (9.5f), (float) (unitX (u + 1) - unitX (u)) });
}

//==============================================================================
//  RoyalSpine
//==============================================================================
const juce::Rectangle<int> RoyalSpine::readoutCard { 16, 156, 280, 110 };
const juce::Rectangle<int> RoyalSpine::royalCard   { 16, 276, 136, 208 };
const juce::Rectangle<int> RoyalSpine::meterCard   { 160, 276, 136, 208 };

RoyalSpine::RoyalSpine (RoyalChainAudioProcessor& p)
    : processor (p)
{
    setOpaque (true);
    auto& apvts = processor.parameters();

    stylePrev.setButtonText (th::utf8 ("\xe2\x80\xb9"));
    styleNext.setButtonText (th::utf8 ("\xe2\x80\xba"));
    stylePrev.setTooltip ("Previous vocal style");
    styleNext.setTooltip ("Next vocal style");
    stylePrev.setTitle ("Previous vocal style");
    styleNext.setTitle ("Next vocal style");
    stylePrev.onClick = [this] { stepStyle (-1); };
    styleNext.onClick = [this] { stepStyle (+1); };
    addAndMakeVisible (stylePrev);
    addAndMakeVisible (styleNext);

    // Items first: a ComboBoxAttachment only maps existing 1-based ids to the
    // choice indices, so the order must match the "style" parameter.
    juce::StringArray styles;
    for (auto* name : th::kStyleNames)
        styles.add (name);
    styleBox.addItemList (styles, 1);
    styleBox.setTextWhenNothingSelected ("VOCAL STYLE");
    styleBox.setTooltip ("Vocal style: sets how each stage treats the voice and how complex the Compass figures are.");
    styleBox.setTitle ("Vocal style");
    addAndMakeVisible (styleBox);
    styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, "style", styleBox);

    sattari::ui::KeyButton::Config cfg;
    cfg.accentId = "royal-chain";
    cfg.showAutoKey = false;
    cfg.showFollow = true;
    cfg.followLabel = "Follow Auto Pitch key";
    cfg.scaleNames = juce::StringArray { "chromatic", "major", "minor", "penta maj", "penta min", "dorian", "mixolydian", "harm minor" };
    // The shared KeyButton draws its hint on a third line that only exists at
    // ~70 px; at the spine's 46 px it would print over the key. Keep the hint as
    // the accessible description instead.
    cfg.hint = {};
    keyButton.setConfig (cfg);
    keyButton.setDescription ("Tune key and scale. Click: follow Auto Pitch / set key + scale.");
    keyButton.onLinkFollow = [this]
    {
        MacroController::setParameterValue (processor.parameters(), "syncFromAutoPitch", 1.0f);
    };
    keyButton.onManualKey = [this] (int root, int scaleIdx)
    {
        auto& st = processor.parameters();
        MacroController::setParameterValue (st, "syncFromAutoPitch", 0.0f);
        MacroController::setParameterValue (st, "tuneKey", (float) root);
        MacroController::setParameterValue (st, "tuneScale", (float) scaleIdx);
    };
    addAndMakeVisible (keyButton);

    const auto& royal = th::macro (th::MacroId::Royal);
    rc::configurePortalSlider (royalSlider, royal, apvts);
    addAndMakeVisible (royalSlider);
    royalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, royal.paramId, royalSlider);
    rc::installTextFunctions (royalSlider, royal);
    royalSlider.addMouseListener (this, false);
    royalSlider.onDragStart = [this]
    {
        setFocusedMacro (th::MacroId::Royal);
        if (onFocusMacro)
            onFocusMacro (th::MacroId::Royal);
    };

    // Deterministic particle seeds for the IN / OUT Portal slots.
    rc::Rng rng { 0x51A7E5u };
    for (auto& s : slots)
        for (int i = 0; i < 14; ++i)
        {
            s.px[i] = rng.uni() * 88.0f;
            s.py[i] = 1.0f + rng.uni() * 5.5f;
            s.alpha[i] = 0.25f + 0.35f * rng.uni();
        }
    refresh();
}

RoyalSpine::~RoyalSpine()
{
    royalSlider.removeMouseListener (this);
}

void RoyalSpine::resized()
{
    stylePrev.setBounds (16, 30, 30, 30);
    styleBox.setBounds (50, 30, 212, 30);
    styleNext.setBounds (266, 30, 30, 30);
    // The shared KeyButton sizes its key text at 62 % of what is left under its
    // chip line: 60 px gives a 15.5 px key (46 px would give 6.8 px).
    keyButton.setBounds (16, 86, 280, 60);
    royalSlider.setBounds (34, 308, 100, 132);
}

void RoyalSpine::mouseEnter (const juce::MouseEvent& e)
{
    if (e.eventComponent == &royalSlider)
    {
        setFocusedMacro (th::MacroId::Royal);
        if (onFocusMacro)
            onFocusMacro (th::MacroId::Royal);
    }
}

void RoyalSpine::stepStyle (int delta)
{
    auto& apvts = processor.parameters();
    const int idx = juce::roundToInt (rawValue (apvts, "style"));
    const int next = ((idx + delta) % th::kNumStyles + th::kNumStyles) % th::kNumStyles;
    MacroController::setParameterValue (apvts, "style", (float) next);
}

void RoyalSpine::setFocusedMacro (th::MacroId id)
{
    if (id == focused)
        return;
    focused = id;
    focusedValueShown = -1.0e9f;
    repaint (readoutCard);
}

void RoyalSpine::refresh()
{
    auto& apvts = processor.parameters();

    // Key: the first UI for tuneKey / tuneScale / syncFromAutoPitch.
    const bool sync = rawValue (apvts, "syncFromAutoPitch") > 0.5f;
    keyButton.setState (sync ? sattari::ui::KeyButton::Mode::Follow : sattari::ui::KeyButton::Mode::Manual,
                        juce::roundToInt (rawValue (apvts, "tuneKey")), juce::roundToInt (rawValue (apvts, "tuneScale")));

    // Readout card follows the focused macro.
    const float v = rawValue (apvts, th::macro (focused).paramId);
    if (std::abs (v - focusedValueShown) > 1.0e-4f)
    {
        focusedValueShown = v;
        repaint (readoutCard);
    }

    // Meters.
    const float dt = 1.0f / 30.0f;
    bool metersDirty = false;
    const float meters[2] = { processor.getInputMeter(), processor.getOutputMeter() };
    for (int i = 0; i < 2; ++i)
    {
        auto& s = slots[i];
        const float db = gainToDb (meters[i]);
        const float fill = clamp01 ((db + 48.0f) / 48.0f);
        if (db >= s.peakDb)
        {
            s.peakDb = db;
            s.peakAge = 0.0f;
        }
        else if ((s.peakAge += dt) > 1.5f)
        {
            s.peakDb = db;
            s.peakAge = 0.0f;
        }
        const float speed = 20.0f + 120.0f * fill;
        for (int k = 0; k < 14; ++k)
        {
            s.px[k] += speed * dt;
            if (s.px[k] > 88.0f)
                s.px[k] -= 88.0f;
        }
        metersDirty = metersDirty || fill > 0.0f || s.fill > 0.0f || std::abs (db - s.db) > 0.05f;
        s.db = db;
        s.fill = fill;
    }

    const float newRows[4] = { clamp01 (processor.getControlMeter()), clamp01 (processor.getSibilanceMeter()),
                               clamp01 (processor.getLimiterMeter()), clamp01 (processor.getWidthMeter()) };
    for (int r = 0; r < 4; ++r)
    {
        rows[r] = newRows[r];
        if (std::abs (rows[r] - rowsShown[r]) > 0.004f)
        {
            rowsShown[r] = rows[r];
            metersDirty = true;
        }
    }
    if (metersDirty)
        repaint (meterCard);
}

void RoyalSpine::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.fillAll (th::col (th::canvas));
    g.setColour (th::col (th::spine));
    g.fillRoundedRectangle (b, 16.0f);
    g.setColour (th::col (th::cardEdge));
    g.drawRoundedRectangle (b.reduced (0.5f), 16.0f, 1.0f);

    drawString (g, "VOCAL STYLE", { 16.0f, 14.0f, 280.0f, 12.0f }, th::caps (9.0f, 0.2f), th::col (th::textDim),
                juce::Justification::centredLeft);
    drawString (g, "KEY", { 16.0f, 70.0f, 280.0f, 12.0f }, th::caps (9.0f, 0.2f), th::col (th::textDim),
                juce::Justification::centredLeft);

    paintReadout (g, readoutCard.toFloat());

    // ROYAL card.
    {
        const auto c = royalCard.toFloat();
        g.setColour (th::col (th::card));
        g.fillRoundedRectangle (c, 10.0f);
        g.setColour (th::col (th::cardEdge));
        g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);
        drawString (g, "ROYAL", { c.getX(), c.getY() + 14.0f, 136.0f, 12.0f }, th::caps (9.0f, 0.2f),
                    th::col (th::gold), juce::Justification::centred);
        drawString (g, "GLOBAL POLISH", { c.getX(), c.getY() + 178.0f, 136.0f, 12.0f }, th::caps (8.0f, 0.2f),
                    th::col (th::textDim), juce::Justification::centred);
    }

    paintMeters (g, meterCard.toFloat());
}

void RoyalSpine::paintReadout (juce::Graphics& g, juce::Rectangle<float> c) const
{
    if (! g.clipRegionIntersects (c.getSmallestIntegerContainer()))
        return;

    g.setColour (th::col (th::card));
    g.fillRoundedRectangle (c, 10.0f);
    g.setColour (th::col (th::cardEdge));
    g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);

    const auto& spec = th::macro (focused);
    const auto colour = th::col (spec.colour);
    const float x = c.getX(), y = c.getY();

    float v = focusedValueShown;
    float frac = clamp01 (v);
    if (auto* prm = processor.parameters().getParameter (spec.paramId))
    {
        v = prm->convertFrom0to1 (prm->getValue());
        frac = prm->getValue();
    }

    drawString (g, spec.caption, { x + 14.0f, y + 12.0f, 136.0f, 12.0f }, th::caps (10.0f), colour, juce::Justification::centredLeft);
    drawString (g, rc::formatMacroValue (spec, v, false), { x + 14.0f, y + 28.0f, 136.0f, 40.0f }, th::numerals (36.0f), colour,
                juce::Justification::centredLeft);
    drawString (g, spec.isDb ? "DB" : "PERCENT", { x + 14.0f, y + 70.0f, 136.0f, 10.0f }, th::caps (8.0f, 0.2f),
                th::col (th::textDim), juce::Justification::centredLeft);

    const auto bar = juce::Rectangle<float> (x + 14.0f, y + 90.0f, 136.0f, 2.0f);
    g.setColour (th::col (th::hairline));
    g.fillRect (bar);
    g.setColour (colour);
    g.fillRect (bar.withWidth (136.0f * clamp01 (frac)));

    g.setColour (th::col (th::hairline));
    g.fillRect (x + 160.0f, y + 12.0f, 1.0f, c.getHeight() - 24.0f);

    g.setColour (th::col (th::textDim));
    g.setFont (juce::Font (th::body (10.0f)));
    g.drawFittedText (spec.description, juce::Rectangle<float> (x + 172.0f, y + 12.0f, 96.0f, c.getHeight() - 22.0f).toNearestInt(),
                      juce::Justification::topLeft, 6, 1.0f);
}

void RoyalSpine::paintMeters (juce::Graphics& g, juce::Rectangle<float> c) const
{
    if (! g.clipRegionIntersects (c.getSmallestIntegerContainer()))
        return;

    g.setColour (th::col (th::card));
    g.fillRoundedRectangle (c, 10.0f);
    g.setColour (th::col (th::cardEdge));
    g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);
    const float x = c.getX(), y = c.getY();

    const char* slotNames[2] = { "IN", "OUT" };
    for (int i = 0; i < 2; ++i)
    {
        const auto& s = slots[i];
        const float top = y + (i == 0 ? 0.0f : 36.0f);
        drawString (g, slotNames[i], { x + 10.0f, top + 14.0f, 26.0f, 12.0f }, th::caps (8.5f), th::col (th::textDim),
                    juce::Justification::centredLeft);

        const auto slot = juce::Rectangle<float> (x + 38.0f, top + 16.0f, 88.0f, 8.0f);
        g.setColour (th::col (th::stage));
        g.fillRoundedRectangle (slot, 4.0f);

        const auto hotColour = s.db >= -0.3f ? juce::Colour (sattari::brand::kDanger)
                             : (s.db > -3.0f ? juce::Colour (sattari::brand::kWarning) : th::col (th::mint));
        const float fillW = 88.0f * s.fill;
        if (fillW > 0.5f)
        {
            g.setColour (hotColour.withAlpha (0.30f));
            g.fillRoundedRectangle (slot.withWidth (juce::jmax (8.0f, fillW)), 4.0f);
            for (int k = 0; k < 14; ++k)
                if (s.px[k] < fillW - 1.5f)
                {
                    g.setColour (hotColour.withAlpha (s.alpha[k]));
                    g.fillRect (slot.getX() + s.px[k], slot.getY() + s.py[k], 1.5f, 1.5f);
                }
        }
        if (s.peakDb > -48.0f)
        {
            const float px = slot.getX() + 88.0f * clamp01 ((s.peakDb + 48.0f) / 48.0f);
            g.setColour (th::col (th::text).withAlpha (0.7f));
            g.fillRect (juce::jmin (px, slot.getRight() - 1.0f), slot.getY() + 1.0f, 1.0f, 6.0f);
        }
        g.setColour (th::col (th::cardEdge));
        g.drawRoundedRectangle (slot.reduced (0.5f), 4.0f, 1.0f);

        const juce::String value = s.db <= -99.0f ? th::utf8 ("\xe2\x88\x92\xe2\x88\x9e dB")
                                                  : signedOneDecimal (s.db) + " dB";
        drawString (g, value, { x + 38.0f, top + 28.0f, 88.0f, 12.0f }, th::body (9.0f), th::col (th::text),
                    juce::Justification::centredRight);
    }

    g.setColour (th::col (th::hairline));
    g.fillRect (x + 10.0f, y + 88.0f, 116.0f, 1.0f);

    struct Row { const char* label; std::uint32_t colour; };
    const Row rowSpec[4] = { { "GR", th::kNodes[1].colour }, { "DE-ESS", th::kNodes[2].colour },
                             { "LIMIT", th::gold }, { "WIDTH", th::kNodes[4].colour } };
    for (int r = 0; r < 4; ++r)
    {
        const float ry = y + 100.0f + 24.0f * (float) r;
        drawString (g, rowSpec[r].label, { x + 10.0f, ry, 50.0f, 14.0f }, th::caps (8.5f), th::col (th::textDim),
                    juce::Justification::centredLeft);
        const auto bar = juce::Rectangle<float> (x + 60.0f, ry + 5.0f, 36.0f, 4.0f);
        g.setColour (th::col (th::track));
        g.fillRoundedRectangle (bar, 2.0f);
        const float v = rowsShown[r] < 0.0f ? 0.0f : rowsShown[r];
        if (v > 0.001f)
        {
            g.setColour (th::col (rowSpec[r].colour));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, 36.0f * v)), 2.0f);
        }

        juce::String text;
        if (r == 0)      text = signedOneDecimal (-v * 12.0f);
        else if (r == 2) text = signedOneDecimal (20.0f * std::log10 (1.0f - v / 6.0f));
        else             text = juce::String (juce::roundToInt (v * 100.0f)) + "%";
        drawString (g, text, { x + 98.0f, ry, 28.0f, 14.0f }, th::body (9.0f), th::col (th::text),
                    juce::Justification::centredRight);
    }
}

void RoyalSpine::appendCaptionChecks (std::vector<th::CaptionCheck>& out) const
{
    out.push_back ({ "spine eyebrow", "VOCAL STYLE", th::caps (9.0f, 0.2f), 280.0f });
    out.push_back ({ "spine eyebrow", "KEY", th::caps (9.0f, 0.2f), 280.0f });
    out.push_back ({ "royal eyebrow", "ROYAL", th::caps (9.0f, 0.2f), 136.0f });
    out.push_back ({ "royal caption", "GLOBAL POLISH", th::caps (8.0f, 0.2f), 136.0f });
    for (const auto& m : th::kMacros)
    {
        out.push_back ({ "readout caption", m.caption, th::caps (10.0f), 136.0f });
        out.push_back ({ "readout value", m.isDb ? th::utf8 ("\xe2\x88\x92") + "18.0" : juce::String ("100"), th::numerals (36.0f), 136.0f });
    }
    out.push_back ({ "readout unit", "PERCENT", th::caps (8.0f, 0.2f), 136.0f });
    out.push_back ({ "meter label", "OUT", th::caps (8.5f), 26.0f });
    out.push_back ({ "meter value", th::utf8 ("\xe2\x88\x92") + "48.0 dB", th::body (9.0f), 88.0f });
    for (const char* label : { "GR", "DE-ESS", "LIMIT", "WIDTH" })
        out.push_back ({ "meter row", label, th::caps (8.5f), 50.0f });
    out.push_back ({ "meter row value", th::utf8 ("\xe2\x88\x92") + "12.0", th::body (9.0f), 28.0f });
    out.push_back ({ "meter row value", "100%", th::body (9.0f), 28.0f });
}
