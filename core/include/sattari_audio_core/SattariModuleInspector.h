#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <sattari_audio_core/SattariBranding.h>

#include <memory>
#include <vector>

// =============================================================================
//  Generic rack-module parameter inspector.
// -----------------------------------------------------------------------------
//  Rack hosts have a fixed APVTS containing every module's namespaced controls.
//  This component exposes the selected module's real parameters without asking
//  each rack shell to duplicate dozens of bespoke editors.
// =============================================================================

namespace sattari::ui
{

class SattariModuleInspector final : public juce::Component
{
public:
    explicit SattariModuleInspector (juce::AudioProcessorValueTreeState& state, bool useGrid = false)
        : apvts (state), gridLayout (useGrid)
    {
        heading.setText ("Select a module", juce::dontSendNotification);
        heading.setFont (juce::Font (sattari::brand::fontOptions (16.0f, juce::Font::bold)));
        addAndMakeVisible (heading);
        setTitle ("Module controls");
        setDescription ("Parameters for the selected rack module");
        setAccessible (true);
        setSize (340, 80);
    }

    void showModule (const char* moduleId, const char* displayName,
                     bool controlsAreShared = false)
    {
        rows.clear();

        if (moduleId == nullptr || displayName == nullptr)
        {
            heading.setText (gridLayout ? "Empty slot" : "Select a module", juce::dontSendNotification);
            setSize (juce::jmax (getWidth(), 320), 80);
            resized();
            return;
        }

        heading.setText (juce::String (displayName)
                         + (controlsAreShared ? " · shared controls" : gridLayout ? "" : " · controls"),
                         juce::dontSendNotification);

        const juce::String prefix = juce::String (moduleId) + ".";
        for (auto* parameter : apvts.processor.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
            if (withId == nullptr || ranged == nullptr
                || ! withId->paramID.startsWith (prefix))
                continue;

            auto row = std::make_unique<Row>();
            row->label = std::make_unique<juce::Label>();
            row->label->setText (parameter->getName (64), juce::dontSendNotification);
            row->label->setJustificationType (juce::Justification::centredLeft);
            row->label->setTitle (parameter->getName (64));
            if (gridLayout) row->label->setFont (sattari::brand::fontOptions (12.0f));
            addAndMakeVisible (*row->label);

            if (dynamic_cast<juce::AudioParameterBool*> (parameter) != nullptr)
            {
                row->button = std::make_unique<juce::ToggleButton> ("Enabled");
                row->button->setTitle (parameter->getName (64));
                row->button->setDescription ("Toggle " + parameter->getName (64));
                addAndMakeVisible (*row->button);
                row->buttonAttachment = std::make_unique<ButtonAttachment> (
                    apvts, withId->paramID, *row->button);
            }
            else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
            {
                row->combo = std::make_unique<juce::ComboBox>();
                row->combo->addItemList (choice->choices, 1);
                row->combo->setTitle (parameter->getName (64));
                row->combo->setDescription ("Choose " + parameter->getName (64));
                addAndMakeVisible (*row->combo);
                row->comboAttachment = std::make_unique<ComboAttachment> (
                    apvts, withId->paramID, *row->combo);
            }
            else
            {
                row->slider = std::make_unique<juce::Slider>();
                row->slider->setSliderStyle (gridLayout ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
                row->slider->setTextBoxStyle (gridLayout ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight,
                                              false, gridLayout ? 94 : 72, gridLayout ? 24 : 20);
                row->slider->setScrollWheelEnabled (! gridLayout);
                const auto range = ranged->getNormalisableRange();
                row->slider->setRange (range.start, range.end, range.interval);
                if (range.skew != 1.0f)
                    row->slider->setSkewFactor (range.skew, range.symmetricSkew);
                const int decimals = range.interval >= 1.0f ? 0
                                   : range.interval >= 0.1f ? 1
                                   : range.interval >= 0.01f ? 2 : 3;
                row->slider->setNumDecimalPlacesToDisplay (decimals);
                row->slider->setTextValueSuffix (
                    parameter->getLabel().isNotEmpty()
                        ? " " + parameter->getLabel() : juce::String());
                row->slider->setTitle (parameter->getName (64));
                row->slider->setDescription ("Adjust " + parameter->getName (64));
                addAndMakeVisible (*row->slider);
                row->sliderAttachment = std::make_unique<SliderAttachment> (
                    apvts, withId->paramID, *row->slider);
            }

            rows.push_back (std::move (row));
        }

        setSize (juce::jmax (getWidth(), 320),
                 juce::jmax (80, 42 + (int) rows.size() * 34));
        resized();
    }

    void resized() override
    {
        if (gridLayout)
        {
            const int columns = juce::jlimit (2, 4, (getWidth() - 24) / 160);
            const int height = 56 + ((int) rows.size() + columns - 1) / columns * 140;
            if (getHeight() != juce::jmax (100, height)) { setSize (getWidth(), juce::jmax (100, height)); return; }
            auto area = getLocalBounds().reduced (12, 8);
            heading.setBounds (area.removeFromTop (34));
            heading.setFont (sattari::brand::fontOptions (23.0f, juce::Font::bold));
            area.removeFromTop (12);
            const int width = area.getWidth() / columns;
            for (size_t i = 0; i < rows.size(); ++i)
            {
                auto cell = juce::Rectangle<int> (area.getX() + (int) i % columns * width,
                    area.getY() + (int) i / columns * 140, width - 12, 130);
                auto& row = rows[i];
                row->label->setBounds (cell.removeFromTop (22));
                row->label->setJustificationType (juce::Justification::centred);
                if (row->slider) row->slider->setBounds (cell);
                if (row->combo) row->combo->setBounds (cell.withHeight (34).translated (0, 24));
                if (row->button) row->button->setBounds (cell.withHeight (32).translated (8, 24));
            }
            return;
        }
        auto bounds = getLocalBounds().reduced (8);
        heading.setBounds (bounds.removeFromTop (26));
        bounds.removeFromTop (6);

        for (auto& row : rows)
        {
            auto line = bounds.removeFromTop (28);
            row->label->setBounds (line.removeFromLeft (
                juce::jlimit (100, 160, getWidth() / 3)));
            line.removeFromLeft (8);
            if (row->button) row->button->setBounds (line);
            if (row->combo)  row->combo->setBounds (line);
            if (row->slider) row->slider->setBounds (line);
            bounds.removeFromTop (6);
        }
    }

private:
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Row
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<ButtonAttachment> buttonAttachment;
        std::unique_ptr<ComboAttachment> comboAttachment;
        std::unique_ptr<SliderAttachment> sliderAttachment;
    };

    juce::AudioProcessorValueTreeState& apvts;
    bool gridLayout = false;
    juce::Label heading;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariModuleInspector)
};

} // namespace sattari::ui
