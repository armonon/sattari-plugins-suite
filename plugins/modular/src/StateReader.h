#pragma once
#include <JuceHeader.h>
#include <cstring>

namespace sattari::modular
{
// Bound JUCE's recursive ValueTree/var decoder before it allocates. Modular's
// v1 schema uses scalar properties and PCM blobs, never arrays or objects.
class StateReader
{
public:
    static juce::ValueTree read (const void* data, size_t size)
    {
        if (data == nullptr || size == 0 || size > 32 * 1024 * 1024) return {};
        StateReader reader (data, size);
        if (! reader.tree (0) || reader.position != size) return {};
        return juce::ValueTree::readFromData (data, size);
    }

private:
    StateReader (const void* data, size_t length) : bytes (static_cast<const unsigned char*> (data)), size (length) {}
    bool integer (unsigned& value)
    {
        if (position == size) return false;
        const unsigned count = bytes[position++];
        if (count > 4 || count > size - position) return false;
        value = 0;
        for (unsigned i = 0; i < count; ++i) value |= (unsigned) bytes[position++] << (8u * i);
        return value <= 0x7fffffffu;
    }
    bool string (size_t maximum)
    {
        const auto remaining = juce::jmin (maximum + 1, size - position);
        const auto* end = static_cast<const unsigned char*> (std::memchr (bytes + position, 0, remaining));
        if (end == nullptr || end == bytes + position) return false;
        const auto length = (size_t) (end - (bytes + position));
        if (! juce::CharPointer_UTF8::isValidString (reinterpret_cast<const char*> (bytes + position), (int) length)) return false;
        position += length + 1;
        return true;
    }
    bool property()
    {
        unsigned length = 0;
        if (! integer (length) || length == 0 || length > size - position) return false;
        const unsigned marker = bytes[position];
        // Marker values are the pinned JUCE binary var format.
        const bool valid = (marker == 1 && length == 5)
            || ((marker == 2 || marker == 3) && length == 1)
            || ((marker == 4 || marker == 6) && length == 9)
            || (marker == 5 && length >= 2 && length <= 4098 && bytes[position + length - 1] == 0
                && std::memchr (bytes + position + 1, 0, length - 2) == nullptr
                && juce::CharPointer_UTF8::isValidString (reinterpret_cast<const char*> (bytes + position + 1), (int) length - 2))
            || (marker == 8 && length <= 1440000 * 2 * sizeof (float) + 1);
        if (! valid) return false;
        position += length;
        return true;
    }
    bool tree (unsigned depth)
    {
        if (depth > 6 || ++nodes > 2048 || ! string (64)) return false;
        unsigned properties = 0, children = 0;
        if (! integer (properties) || properties > 16) return false;
        for (unsigned i = 0; i < properties; ++i) if (! string (64) || ! property()) return false;
        if (! integer (children) || children > 2048 - nodes) return false;
        for (unsigned i = 0; i < children; ++i) if (! tree (depth + 1)) return false;
        return true;
    }
    const unsigned char* bytes;
    size_t size, position = 0;
    unsigned nodes = 0;
};
}
