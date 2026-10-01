#include "PluginProcessor.h"
#include <sattari_audio_core/SattariEditorRender.h>
#include <sattari_audio_core/SattariRackEditor.h>
#include <cstdio>
#include <set>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& message)
{
    if (! ok) { ++failures; std::fprintf (stderr, "FAIL: %s\n", message.toRawUTF8()); }
}

template <typename T>
T* find (juce::Component& parent, const juce::String& title = {})
{
    if (auto* result = dynamic_cast<T*> (&parent))
        if (title.isEmpty() || result->getTitle() == title) return result;
    for (auto* child : parent.getChildren())
        if (auto* result = find<T> (*child, title)) return result;
    return nullptr;
}

void checkLayout (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (! child->isVisible()) continue;
        const bool control = dynamic_cast<juce::Button*> (child) || dynamic_cast<juce::Slider*> (child)
            || dynamic_cast<juce::ComboBox*> (child) || dynamic_cast<juce::TextEditor*> (child);
        if (control && child->getTitle().isNotEmpty())
        {
            check (child->getWidth() > 0 && child->getHeight() > 0, child->getTitle() + " has positive size");
            check (parent.getLocalBounds().contains (child->getBounds()), child->getTitle() + " fits its parent");
        }
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
        {
            const auto shown = slider->getTextFromValue (slider->getValue());
            const double tolerance = juce::jmax (slider->getInterval(), 0.01 * (slider->getMaximum() - slider->getMinimum()));
            check (std::abs (slider->getValueFromText (shown) - slider->getValue()) <= tolerance,
                   child->getTitle() + " display units round-trip to parameter values");
        }
        if (auto* table = dynamic_cast<juce::TableListBox*> (child))
            check (table->getHeader().getTotalWidth() <= table->getWidth(), "table columns fit the default viewport");
        checkLayout (*child);
    }
}

void render (juce::AudioProcessorEditor& editor, const juce::File& folder, const juce::String& name)
{
    editor.resized(); checkLayout (editor);
    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
    std::set<juce::uint32> colours;
    int bright = 0;
    for (int y = 0; y < image.getHeight(); y += 8)
        for (int x = 0; x < image.getWidth(); x += 8)
        {
            auto pixel = image.getPixelAt (x, y); colours.insert (pixel.getARGB());
            if (pixel.getBrightness() > 0.65f) ++bright;
        }
    check (colours.size() > 30 && bright > 20, name + " contains rendered controls and text");
    const int headerOffset = juce::String (RELEASE_UI_SLUG) == "mix" ? 36 : 0;
    int logoPixels = 0;
    for (int y = 20; y < 52; ++y)
        for (int x = 22; x < 108; ++x)
            if (image.getPixelAt (x, y + headerOffset).getBrightness() > 0.6f) ++logoPixels;
    check (sattari::branding::logo().isValid() && logoPixels > 60, name + " displays the embedded Sattari logo");
    if (folder != juce::File())
        check (sattari::test::renderComponentToPng (editor, folder.getChildFile (name + ".png")), name + " PNG written");
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    RELEASE_UI_PROCESSOR processor;
    processor.prepareToPlay (48000, 256);
    const auto slug = juce::String (RELEASE_UI_SLUG);
    const juce::File folder = argc > 1 ? juce::File (argv[1]) : juce::File();
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    check (editor != nullptr, "editor exists");
    if (! editor) return 1;
    editor->setVisible (true);
    auto* constrainer = editor->getConstrainer();
    check (constrainer != nullptr, "resize constraints exist");
    if (! constrainer) return 1;
    const juce::Point<int> sizes[] = {
        { constrainer->getMinimumWidth(), constrainer->getMinimumHeight() },
        { editor->getWidth(), editor->getHeight() },
        { constrainer->getMaximumWidth(), constrainer->getMaximumHeight() }
    };
    for (int i = 0; i < 3; ++i)
    {
        editor->setSize (sizes[i].x, sizes[i].y);
        render (*editor, folder, slug + "-" + juce::String (i));
    }

    if (auto* tabs = find<juce::TabbedComponent> (*editor)) tabs->setCurrentTabIndex (1);
    if (auto* rack = find<sattari::ui::SattariRackEditor> (*editor))
    {
        auto& params = processor.parameters();
        auto* factory = find<juce::ComboBox> (*rack, "Factory preset");
        check (factory != nullptr, "rack factory preset control exists");
        if (factory) factory->setSelectedItemIndex (1, juce::sendNotificationSync);
        auto* first = params.getParameter (sattari::rack::SattariRackProcessor::slotParamId (0));
        auto* second = params.getParameter (sattari::rack::SattariRackProcessor::slotParamId (1));
        const float a = first->getValue(), b = second->getValue();
        auto* firstBypass = params.getParameter (sattari::rack::SattariRackProcessor::slotBypassParamId (0));
        auto* secondBypass = params.getParameter (sattari::rack::SattariRackProcessor::slotBypassParamId (1));
        firstBypass->setValueNotifyingHost (1.0f);
        secondBypass->setValueNotifyingHost (0.0f);
        auto* move = find<juce::Button> (*rack, "Move slot 1 down");
        check (move != nullptr && bool (move->onClick), "slot reorder is wired");
        if (move && move->onClick) move->onClick();
        check (std::abs (first->getValue() - b) < 1.0e-6f && std::abs (second->getValue() - a) < 1.0e-6f,
               "slot order changes the real APVTS parameters");
        check (firstBypass->getValue() < 0.5f && secondBypass->getValue() > 0.5f,
               "reordering carries bypass state with the module");
        secondBypass->setValueNotifyingHost (0.0f);
        auto* select = find<juce::Button> (*rack, "Slot 2");
        check (select != nullptr && bool (select->onClick), "slot selection is wired");
        if (select && select->onClick) select->onClick();
        auto* inspector = find<sattari::ui::SattariModuleInspector> (*rack);
        auto* slider = inspector != nullptr ? find<juce::Slider> (*inspector) : nullptr;
        check (slider != nullptr, "selected module exposes numeric controls");
        if (slider)
        {
            const double value = slider->getValue();
            std::vector<float> before;
            for (auto* parameter : processor.getParameters()) before.push_back (parameter->getValue());
            slider->setValue (std::abs (value - slider->getMaximum()) < 1.0e-6 ? slider->getMinimum() : slider->getMaximum(), juce::sendNotificationSync);
            check (std::abs (slider->getValue() - value) > 1.0e-6, "inspector control accepts edits");
            bool parameterChanged = false;
            for (int i = 0; i < processor.getParameters().size(); ++i)
                parameterChanged |= std::abs (processor.getParameters()[i]->getValue() - before[(size_t) i]) > 1.0e-6f;
            check (parameterChanged, "inspector edits reach processor parameters");
        }
        for (int i = 0; i < 3; ++i)
        {
            editor->setSize (sizes[i].x, sizes[i].y);
            render (*editor, folder, slug + "-chain-" + juce::String (i));
        }
    }
    else
    {
        const auto parameterId = slug == "sattari-arp" ? "gate" : "amount";
        const auto title = slug == "sattari-arp" ? "Gate" : "Sub amount";
        auto* slider = find<juce::Slider> (*editor, title);
        check (slider != nullptr, "main macro exists");
        if (slider)
        {
            const auto value = slider->getMinimum() + 0.5 * (slider->getMaximum() - slider->getMinimum());
            slider->setValue (value, juce::sendNotificationSync);
            check (std::abs (processor.parameters().getRawParameterValue (parameterId)->load() - (float) value) < 0.01f,
                   "macro edits reach the real processor parameter");
        }
        auto* factory = find<juce::ComboBox> (*editor, "Factory presets");
        check (factory != nullptr, "factory preset menu exists");
        if (factory) factory->setSelectedItemIndex (1, juce::sendNotificationSync);
        editor->setSize (sizes[0].x, sizes[0].y);
        render (*editor, folder, slug + "-preset-min");
    }
    std::printf ("%s UI contract: %d failure(s)\n", RELEASE_UI_SLUG, failures);
    return failures == 0 ? 0 : 1;
}
