#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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
    explicit SattariModuleInspector (juce::AudioProcessorValueTreeState& state)
        : apvts (state)
    {
        heading.setText ("Select a module", juce::dontSendNotification);
        heading.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
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
            heading.setText ("Select a module", juce::dontSendNotification);
            setSize (juce::jmax (getWidth(), 320), 80);
            resized();
            return;
        }

        heading.setText (juce::String (displayName)
                         + (controlsAreShared ? " · shared controls" : " · controls"),
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
                row->slider->setSliderStyle (juce::Slider::LinearHorizontal);
                row->slider->setTextBoxStyle (juce::Slider::TextBoxRight,
                                              false, 72, 20);
                const auto range = ranged->getNormalisableRange();
                row->slider->setRange (range.start, range.end, range.interval);
                if (range.skew != 1.0f)
                    row->slider->setSkewFactor (range.skew, range.symmetricSkew);
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
    juce::Label heading;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SattariModuleInspector)
};

} // namespace sattari::ui
