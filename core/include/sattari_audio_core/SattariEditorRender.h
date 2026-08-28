#pragma once

// Headless visual-contract helper. Test executables use this to render the real
// editor component tree to PNG, allowing layout review without launching a DAW.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

#include <memory>

namespace sattari::test
{

inline bool renderComponentToPng (juce::Component& component,
                                  const juce::File& destination)
{
    if (component.getWidth() <= 0 || component.getHeight() <= 0)
        return false;

    component.resized();
    juce::Image image (
        juce::Image::ARGB, component.getWidth(), component.getHeight(), true);
    juce::Graphics graphics (image);
    component.paintEntireComponent (graphics, true);

    if (destination.getParentDirectory().createDirectory().failed())
        return false;
    destination.deleteFile();
    juce::FileOutputStream stream (destination);
    if (! stream.openedOk())
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream);
}

inline bool renderEditorToPng (juce::AudioProcessor& processor,
                               const juce::File& destination,
                               int width = 0,
                               int height = 0)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
        return false;

    if (width > 0 && height > 0)
        editor->setSize (width, height);
    return renderComponentToPng (*editor, destination);
}

} // namespace sattari::test
