#pragma once

#include <string>

#if defined(_WIN32)
 #include <filesystem>
#endif

namespace sattari::onnx
{

inline auto nativeModelPath (const std::string& utf8Path)
{
   #if defined(_WIN32)
    return std::filesystem::u8path (utf8Path).native();
   #else
    return utf8Path;
   #endif
}

} // namespace sattari::onnx
