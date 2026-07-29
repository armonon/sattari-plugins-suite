#pragma once

// Headless visual-contract helper. Test executables use this to render the real
// editor component tree to PNG, allowing layout review without launching a DAW.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>

#include <memory>

namespace sattari::test
{

inline bool renderEditorToPng (juce::AudioProcessor& processor,
                               const juce::File& destination)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr || editor->getWidth() <= 0 || editor->getHeight() <= 0)
        return false;

    editor->resized();
    juce::Image image (
        juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics graphics (image);
    editor->paintEntireComponent (graphics, true);

    if (destination.getParentDirectory().createDirectory().failed())
        return false;
    destination.deleteFile();
    juce::FileOutputStream stream (destination);
    if (! stream.openedOk())
        return false;

    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream);
}

} // namespace sattari::test
